#include "Config.h"
#include <array>
#include <csignal>
#include <cstring>
#include <execinfo.h>
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

void handler(int sig) {
  void *array[10];
  size_t size;

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
    AVCodecParameters *pLocalCodecParameters = pFormatCtx->streams[i]->codecpar;
    AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

    // specific for video and audio
    if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      cout << "Video Codec: resolution " << pLocalCodecParameters->width << " x " << pLocalCodecParameters->height << endl;
    } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      cout << "Audio Codec: " << pLocalCodecParameters->channels << " channels, sample rate " <<  pLocalCodecParameters->sample_rate << endl;
    }
    // general
    printf("\tCodec %s ID %d bit_rate %lld", pLocalCodec->long_name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
  }

  avformat_free_context(pFormatCtx);

  return EXIT_SUCCESS;
}