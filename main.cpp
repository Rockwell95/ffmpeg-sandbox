#include "Config.h"
#include "KGCodec.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
}

using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;

std::condition_variable cv;
std::mutex cv_m;
std::atomic currentFrameTime{std::chrono::system_clock::now()};
AVPacket packet;

void readFrames(AVFormatContext *pInFormatCtx, AVFormatContext *pOutFormatCtx, int *streamsList, int numStreams);
void tReadFrame(AVFormatContext *pInFormatCtx, AVFormatContext *pOutFormatCtx, int numStreams, int *streamsList);
void timeout();

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
  AVFormatContext *inputFormatContext = avformat_alloc_context();
  AVFormatContext *outputFormatContext = nullptr;

  signal(SIGBUS, handler);

  cout << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR << endl;

#if CONFIG_AVDEVICE
  avdevice_register_all();
#endif
  avformat_network_init();
  cout << argv[1] << endl;

  avformat_open_input(&inputFormatContext, argv[1], nullptr, nullptr);

  cout << "Format: " << inputFormatContext->iformat->long_name << ", duration: " << inputFormatContext->duration << "μs" << endl;

  avformat_find_stream_info(inputFormatContext, nullptr);

  avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, "out.ts");
  if (!outputFormatContext) {
    cerr << "Could not create output context!" << endl;
    return AVERROR_UNKNOWN;
  }

  int numStreams = inputFormatContext->nb_streams;
  int *streamsList = nullptr;
  streamsList = static_cast<int *>(av_calloc(numStreams, sizeof(*streamsList)));

  readFrames(inputFormatContext, outputFormatContext, streamsList, numStreams);

  avformat_close_input(&inputFormatContext);

  if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&outputFormatContext->pb);
  }
  avformat_free_context(outputFormatContext);
  av_freep(&streamsList);

  return EXIT_SUCCESS;
}
void readFrames(AVFormatContext *pInFormatCtx, AVFormatContext *pOutFormatCtx, int *streamsList, int numStreams) {
  int streamIndex = 0;
  for (size_t i = 0; i < pInFormatCtx->nb_streams; i++) {
    AVStream *outStream;
    const AVStream *inStream = pInFormatCtx->streams[i];
    const AVCodecParameters *inCodecPar = inStream->codecpar;
    if (inCodecPar->codec_type != AVMEDIA_TYPE_VIDEO && inCodecPar->codec_type != AVMEDIA_TYPE_DATA) {
      streamsList[i] = -1;
      continue;
    }

    streamsList[i] = streamIndex++;
    outStream = avformat_new_stream(pOutFormatCtx, nullptr);

    if (!outStream) {
      cerr << "Failed " << endl;
      exit(AVERROR_UNKNOWN);
    }
    const int codecCopyStatus = avcodec_parameters_copy(outStream->codecpar, inCodecPar);
    if (codecCopyStatus < 0) {
      cerr << "Failed to copy codec params" << endl;
      exit(AVERROR_UNKNOWN);
    }
  }

  if (!(pOutFormatCtx->oformat->flags & AVFMT_NOFILE)) {
    const int avioOpen = avio_open(&pOutFormatCtx->pb, "out.ts", AVIO_FLAG_WRITE);
    if (avioOpen < 0) {
      cerr << "Could not open output file out.ts" << endl;
      exit(AVERROR_UNKNOWN);
    }
  }
  if (const int headerWriteStatus = avformat_write_header(pOutFormatCtx, nullptr); headerWriteStatus < 0) {
    cerr << "Error occurred when opening output file" << endl;
    exit(1);
  }

  std::thread tRead(tReadFrame, pInFormatCtx, pOutFormatCtx, numStreams, streamsList);
  std::thread tTimeout(timeout);
  tRead.join();
  tTimeout.join();

  cout << "Joined Thread" << endl;

  av_write_trailer(pOutFormatCtx);
}

void tReadFrame(AVFormatContext *pInFormatCtx, AVFormatContext *pOutFormatCtx, int numStreams, int *streamsList) {
  cout << "Packet Reader Joined" << endl;
  while (true) {
    av_packet_unref(&packet);
    const AVStream *inStream;
    const AVStream *outStream;
    int readFrameResult;

    // This call blocks when the stream stops.
    // TODO: Have the secondary thread push something else through until the stream resumes.
    readFrameResult = av_read_frame(pInFormatCtx, &packet);
    currentFrameTime = std::chrono::system_clock::now();
    if (readFrameResult < 0) {
      break;
    }

    // This section updates the packet and writes it.
    // TODO: Use this logic to fill in gaps in the video stream
    inStream = pInFormatCtx->streams[packet.stream_index];
    if (packet.stream_index >= numStreams || streamsList[packet.stream_index] < 0) {
      av_packet_unref(&packet);
      continue;
    }
    packet.stream_index = streamsList[packet.stream_index];
    outStream = pOutFormatCtx->streams[packet.stream_index];

    auto options = static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, options);
    packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, options);
    packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);

    packet.pos = -1;

    // cout << "PTS: " << packet.pts << "; DTS: " << packet.dts << "; FLAGS: " << packet.flags << endl;

    if (const int writeFrameStatus = av_interleaved_write_frame(pOutFormatCtx, &packet); writeFrameStatus < 0) {
      cerr << "Error Muxing Packet" << endl;
    }
  }
}

void timeout() {
  cout << "Timeout thread joined!" << endl;
  const auto maxDelay = std::chrono::milliseconds(100);
  while (true) {
    // Try to save the CPU a little by slowing down to running every 5ms
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto now = std::chrono::system_clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - currentFrameTime.load());
    if (diff > maxDelay) {
      cout << "Too much time has passed: " << diff.count() << "ms " << endl;
      cout << packet.pts << endl;
      // TODO: Start pushing through filler packets until the stream resumes.
    } else {
      // TODO: Stop pushing filler data through if doing so.
      cout << "shqip: " << packet.pts << endl;
    }
  }
}