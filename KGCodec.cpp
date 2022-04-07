//
// Created by Nick Mancini on 2022-04-06.
//

#include "KGCodec.h"
KGCodec::KGCodec(const AVCodec &codec, const AVCodecParameters &codecParams) {
  AVCodecContext* codecContext = avcodec_alloc_context3(&codec);
  avcodec_parameters_to_context(codecContext, &codecParams);
  avcodec_open2(codecContext, &codec, nullptr);

  AVPacket* pPacket = av_packet_alloc();
  AVFrame* pFrame = av_frame_alloc();

  avcodec_free_context(&codecContext);
  av_packet_free(&pPacket);
  av_frame_free(&pFrame);
}
