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
#include <easylogging++.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
}

using std::atomic;

std::mutex mutex;
static AVPacket packet;
static AVPacket fallbackPacket;
static AVFormatContext *inputFormatContext = avformat_alloc_context();
static AVFormatContext *fallbackFormatContext = avformat_alloc_context();
static AVFormatContext *outputFormatContext = nullptr;
static atomic currentFrameTime{std::chrono::system_clock::now()};
static atomic<long> lastDts{0};
static atomic<long> lastPts{0};
static atomic<long> lastDuration{0};

void readFrames(int *streamsList, int numStreams, const char *outputSource);
void tReadFrame(int numStreams, int *streamsList);
void timeout();
void writeNewFrame(AVPacket &pkt);
void writeFillerFrame();

INITIALIZE_EASYLOGGINGPP

int main(int argc, char *argv[]) {

  el::Configurations defaultConf;
  defaultConf.setGlobally(el::ConfigurationType::Format, "[%datetime{%Y-%M-%d %H:%m:%s.%g}] [%level] %msg");
  el::Loggers::reconfigureLogger("default", defaultConf);

  if (argc < 4) {
    std::cout << "usage: ./FFMPEGSandbox <primary source> <fallback source> <output>" << std::endl;
    return EXIT_FAILURE;
  }

  LOG(INFO) << "FFMPEG Sandbox Version " << FFMPEGSandbox_VERSION_MAJOR << "." << FFMPEGSandbox_VERSION_MINOR;

#if CONFIG_AVDEVICE
  avdevice_register_all();
#endif
  avformat_network_init();
  const char *primarySource = argv[1];
  const char *fallbackSource = argv[2];
  const char *outputSource = argv[3];
  LOG(INFO) << primarySource;
  LOG(INFO) << fallbackSource;

  avformat_open_input(&inputFormatContext, primarySource, nullptr, nullptr);
  avformat_open_input(&fallbackFormatContext, fallbackSource, nullptr, nullptr);

  LOG(INFO) << "Input Format: " << inputFormatContext->iformat->long_name << ", duration: " << inputFormatContext->duration << "μs";
  LOG(INFO) << "Fallback Format: " << fallbackFormatContext->iformat->long_name << ", duration: " << fallbackFormatContext->duration << "μs";

  avformat_find_stream_info(inputFormatContext, nullptr);

  avformat_alloc_output_context2(&outputFormatContext, nullptr, nullptr, "out.ts");
  if (!outputFormatContext) {
    LOG(ERROR)  << "Could not create output context!";
    return AVERROR_UNKNOWN;
  }

  int numStreams = inputFormatContext->nb_streams;
  int *streamsList = nullptr;
  streamsList = static_cast<int *>(av_calloc(numStreams, sizeof(*streamsList)));

  readFrames(streamsList, numStreams, outputSource);

  avformat_close_input(&inputFormatContext);
  avformat_close_input(&fallbackFormatContext);

  if (outputFormatContext && !(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&outputFormatContext->pb);
  }
  avformat_free_context(outputFormatContext);
  av_freep(&streamsList);

  return EXIT_SUCCESS;
}
void readFrames(int *streamsList, int numStreams, const char *outputSource) {
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
      LOG(ERROR)  << "Failed ";
      exit(AVERROR_UNKNOWN);
    }
    const int codecCopyStatus = avcodec_parameters_copy(outStream->codecpar, inCodecPar);
    if (codecCopyStatus < 0) {
      LOG(ERROR)  << "Failed to copy codec params";
      exit(AVERROR_UNKNOWN);
    }
  }

  if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
    // UDP PORT GOES HERE
    const int avioOpen = avio_open(&outputFormatContext->pb, outputSource, AVIO_FLAG_WRITE);
    if (avioOpen < 0) {
      LOG(ERROR)  << "Could not open output file out.ts";
      exit(AVERROR_UNKNOWN);
    }
  }
  if (const int headerWriteStatus = avformat_write_header(outputFormatContext, nullptr); headerWriteStatus < 0) {
    LOG(ERROR)  << "Error occurred when opening output file";
    exit(1);
  }

  std::jthread tRead(tReadFrame, numStreams, streamsList);
  std::jthread tTimeout(timeout);
  tRead.join();
  tTimeout.join();

  LOG(INFO) << "Joined Thread";

  av_write_trailer(outputFormatContext);
}

void tReadFrame(int numStreams, int *streamsList) {
  LOG(INFO) << "Packet Reader Joined";
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

    // LOG(INFO) << "PTS: " << packet.pts << "; DTS: " << packet.dts << "; FLAGS: " << packet.flags;

    writeNewFrame(packet);
    av_packet_unref(&packet);
  }
}

void writeNewFrame(AVPacket &pkt) {
  std::lock_guard lock(mutex);
  if (const int writeFrameStatus = av_interleaved_write_frame(outputFormatContext, &pkt); writeFrameStatus < 0) {
    LOG(ERROR)  << "Error Muxing Packet";
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
  LOG(INFO) << "Timeout thread joined!";
  const auto maxDelay = std::chrono::milliseconds(66);
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
      LOG(WARNING) << "Too much time has passed: " << diff.count() << "ms ";
      LOG(WARNING) << "PTS: " << lastPts << "; DTS: " << lastDts << "; DURATION: " << lastDuration;
      writeFillerFrame();
    }
    lastFallbackDts = fallbackPacket.dts;
    lastFallbackPts = fallbackPacket.pts;
  }
}