/**
 * @file main.cpp
 * @author Dominick Mancini
 * @brief Small libavformat implementation that reads in two video sources (ideally streams) and outputs a single continuous stream.
 * When the initial source hasn't received data in a given amount of time (currently hardcoded to 100ms), it falls back to the second
 * source which should be guaranteed reliable (i.e., running on the same host) and sends it out instead.
 * @version 0.1
 * @date 2022-04-08
 * 
 * @copyright Copyright (c) 2022
 * 
 */

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
#include <libavutil/imgutils.h>
}

using std::atomic;
using std::cerr;
using std::cout;
using std::endl;
using std::ifstream;

std::condition_variable cv;
std::mutex mutex;
std::atomic currentFrameTime{std::chrono::system_clock::now()};
static AVPacket packet;
static AVPacket fallbackPacket;
static AVFormatContext *inputFormatContext = avformat_alloc_context();
static AVFormatContext *fallbackFormatContext = avformat_alloc_context();
static AVFormatContext *outputFormatContext = nullptr;
static atomic<long> lastDts{0};
static atomic<long> lastPts{0};
static atomic<long> lastDuration{0};

void readFrames(int *streamsList, int numStreams);
void tReadFrame(int numStreams, int *streamsList);
void timeout();
void writeNewFrame(AVPacket &pkt);
void writeFillerFrame();

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

  signal(SIGBUS, handler);

  cout << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR << endl;

#if CONFIG_AVDEVICE
  avdevice_register_all();
#endif
  avformat_network_init();
  cout << argv[1] << endl;

  avformat_open_input(&inputFormatContext, argv[1], nullptr, nullptr);
  avformat_open_input(&fallbackFormatContext, argv[2], nullptr, nullptr);

  cout << "Input Format: " << inputFormatContext->iformat->long_name << ", duration: " << inputFormatContext->duration << "μs" << endl;
  cout << "Fallback Format: " << fallbackFormatContext->iformat->long_name << ", duration: " << fallbackFormatContext->duration << "μs" << endl;

  avformat_find_stream_info(inputFormatContext, nullptr);

  avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, "out.ts");
  if (!outputFormatContext) {
    cerr << "Could not create output context!" << endl;
    return AVERROR_UNKNOWN;
  }

  int numStreams = inputFormatContext->nb_streams;
  int *streamsList = nullptr;
  streamsList = static_cast<int *>(av_calloc(numStreams, sizeof(*streamsList)));

  readFrames(streamsList, numStreams);

  avformat_close_input(&inputFormatContext);
  avformat_close_input(&fallbackFormatContext);

  if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&outputFormatContext->pb);
  }
  avformat_free_context(outputFormatContext);
  av_freep(&streamsList);

  return EXIT_SUCCESS;
}
void readFrames(int *streamsList, int numStreams) {
  int streamIndex = 0;
  for (size_t i = 0; i < inputFormatContext->nb_streams; i++) {
    AVStream *outStream;
    const AVStream *inStream = inputFormatContext->streams[i];
    const AVCodecParameters *inCodecPar = inStream->codecpar;
    if (inCodecPar->codec_type != AVMEDIA_TYPE_VIDEO && inCodecPar->codec_type != AVMEDIA_TYPE_DATA) {
      streamsList[i] = -1;
      continue;
    }

    streamsList[i] = streamIndex++;
    outStream = avformat_new_stream(outputFormatContext, nullptr);

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

  if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
    // UDP PORT GOES HERE
    const int avioOpen = avio_open(&outputFormatContext->pb, "udp://localhost:9004", AVIO_FLAG_WRITE);
    if (avioOpen < 0) {
      cerr << "Could not open output file out.ts" << endl;
      exit(AVERROR_UNKNOWN);
    }
  }
  if (const int headerWriteStatus = avformat_write_header(outputFormatContext, nullptr); headerWriteStatus < 0) {
    cerr << "Error occurred when opening output file" << endl;
    exit(1);
  }

  std::jthread tRead(tReadFrame, numStreams, streamsList);
  std::jthread tTimeout(timeout);
  tRead.join();
  tTimeout.join();

  cout << "Joined Thread" << endl;

  av_write_trailer(outputFormatContext);
}

void tReadFrame(int numStreams, int *streamsList) {
  cout << "Packet Reader Joined" << endl;
  while (true) {
    const AVStream *inStream;
    const AVStream *outStream;
    int readFrameResult;

    readFrameResult = av_read_frame(inputFormatContext, &packet);
    currentFrameTime = std::chrono::system_clock::now();
    if (readFrameResult < 0) {
      break;
    }

    inStream = inputFormatContext->streams[packet.stream_index];
    if (packet.stream_index >= numStreams || streamsList[packet.stream_index] < 0) {
      av_packet_unref(&packet);
      continue;
    }
    packet.stream_index = streamsList[packet.stream_index];
    outStream = outputFormatContext->streams[packet.stream_index];

    auto options = static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, options);
    packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, options);
    packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);

    lastDts = packet.dts;
    lastPts = packet.pts;
    lastDuration = packet.duration;

    packet.pos = -1;

    // cout << "PTS: " << packet.pts << "; DTS: " << packet.dts << "; FLAGS: " << packet.flags << endl;

    writeNewFrame(packet);
    av_packet_unref(&packet);
  }
}

void writeNewFrame(AVPacket &pkt) {
  std::lock_guard lock(mutex);
  if (const int writeFrameStatus = av_interleaved_write_frame(outputFormatContext, &pkt); writeFrameStatus < 0) {
    cerr << "Error Muxing Packet" << endl;
  }
}

void writeFillerFrame() {
  fallbackPacket.pts = lastPts;
  fallbackPacket.dts = lastDts;
  fallbackPacket.duration = lastDuration;

  lastDts = fallbackPacket.dts + 3000;
  lastPts = fallbackPacket.pts + 3000;
  lastDuration = fallbackPacket.duration;

  packet.pos = -1;

  writeNewFrame(fallbackPacket);
  av_packet_unref(&fallbackPacket);
}

void timeout() {
  cout << "Timeout thread joined!" << endl;
  const auto maxDelay = std::chrono::milliseconds(100);
  long lastFallbackDts = 0;
  long lastFallbackPts = 0;
  while (true) {

    int readFrameResult;
    readFrameResult = av_read_frame(fallbackFormatContext, &fallbackPacket);
    if (readFrameResult < 0) {
      break;
    }

    const auto now = std::chrono::system_clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - currentFrameTime.load());
    if (diff > maxDelay && lastFallbackDts != fallbackPacket.dts && lastFallbackPts != fallbackPacket.pts) {
      cout << "Too much time has passed: " << diff.count() << "ms " << endl;
      cout << "PTS: " << lastPts << "; DTS: " << lastDts << "; DURATION: " << lastDuration << endl;
      writeFillerFrame();
    }
    lastFallbackDts = fallbackPacket.dts;
    lastFallbackPts = fallbackPacket.pts;
  }
}