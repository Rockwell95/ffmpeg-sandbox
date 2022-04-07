#include "Config.h"
#include "KGCodec.h"
#include <array>
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
}

using std::cout;
using std::endl;
using std::ifstream;

void readFrames(AVFormatContext *pContext, AVPacket *pPacket, AVCodecContext *codecContext, AVFrame *pFrame);
static void saveGrayFrame(const unsigned char *buf, int wrap, int xsize, int ysize, const std::string &filename);

void handler(int sig) {
  void *array[10];
  int size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 10);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}

int main(int argc, char *argv[]) {
  AVFormatContext *pFormatCtx = avformat_alloc_context();

  signal(SIGBUS, handler);

  cout << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR << endl;

#if CONFIG_AVDEVICE
  avdevice_register_all();
#endif
  avformat_network_init();
  cout << argv[1] << endl;

  avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr);

  cout << "Format: " << pFormatCtx->iformat->long_name << ", duration: " << pFormatCtx->duration << "μs" << endl;

  avformat_find_stream_info(pFormatCtx, nullptr);

  for (size_t i = 0; i < pFormatCtx->nb_streams; i++) {
    const AVCodecParameters *pLocalCodecParameters = pFormatCtx->streams[i]->codecpar;
    const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

    // specific for video and audio
    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      cout << "Video Codec: resolution " << pLocalCodecParameters->width << " x " << pLocalCodecParameters->height
           << endl;
    } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      cout << "Audio Codec: " << pLocalCodecParameters->channels << " channels, sample rate "
           << pLocalCodecParameters->sample_rate << endl;
    }
    // general
    if (pLocalCodec != nullptr) {
      cout << "\tCodec " << pLocalCodec->long_name << " ID " << pLocalCodec->id << " bit_rate "
           << pLocalCodecParameters->bit_rate << endl;

      // -- -- --
      AVCodecContext *pCodecContext = avcodec_alloc_context3(pLocalCodec);
      avcodec_parameters_to_context(pCodecContext, pLocalCodecParameters);
      avcodec_open2(pCodecContext, pLocalCodec, nullptr);

      AVPacket *pPacket = av_packet_alloc();
      AVFrame *pFrame = av_frame_alloc();

      readFrames(pFormatCtx, pPacket, pCodecContext, pFrame);

      avcodec_free_context(&pCodecContext);
      av_packet_free(&pPacket);
      av_frame_free(&pFrame);
      // -- -- --
    } else {
      const AVCodecID pLocalCodecId = pLocalCodecParameters->codec_id;

      cout << "\tCodec \"" << pLocalCodecId << "\" has no known decoders" << endl;
    }
  }

  avformat_free_context(pFormatCtx);

  return EXIT_SUCCESS;
}
void readFrames(AVFormatContext *pContext, AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame) {
  int wroteFrame = 0;
  int frameIdx = 0;
  while (av_read_frame(pContext, pPacket) >= 0) {
    avcodec_send_packet(pCodecContext, pPacket);
    avcodec_receive_frame(pCodecContext, pFrame);
    if (pFrame->pict_type != AV_PICTURE_TYPE_NONE) {
      cout
          << "Frame " << av_get_picture_type_char(pFrame->pict_type) << " (" << pCodecContext->frame_number << ") pts "
          << pFrame->pts << " dts " << pFrame->pkt_dts << " key_frame " << pFrame->key_frame << " [coded_picture_number "
          << pFrame->coded_picture_number << ", display_picture_number " << pFrame->display_picture_number << "]" << endl;

      if (wroteFrame < 5 && pFrame->data[0] != nullptr) {
        std::string name = "pgm/out_" + std::to_string(frameIdx) + ".pgm";
        saveGrayFrame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, name.c_str());
        wroteFrame++;
      }
      frameIdx++;
    }
  }
}

static void saveGrayFrame(const unsigned char *buf, int wrap, int xsize, int ysize, const std::string &filename) {
  //  std::ofstream f;
  //  f.open(filename, std::ios::app | std::ios::binary);
  //  // writing the minimal required header for a pgm file format
  //  // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
  //  f << "P5\n"
  //    << xsize << " " << ysize << "\n"
  //    << 255 << "\n";
  //
  //  // writing line by line
  //  for (size_t i = 0; i < ysize; i++) {
  //    f.write(reinterpret_cast<char*>(&buf), wrap);
  //  }
  //  f.close();

  FILE *f;
  f = fopen(filename.c_str(), "w");
  // writing the minimal required header for a pgm file format
  // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
  fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

  // writing line by line
  for (size_t i = 0; i < ysize; i++) {
    fwrite(buf + i * wrap, 1, xsize, f);
  }
  fclose(f);
}
