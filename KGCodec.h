//
// Created by Nick Mancini on 2022-04-06.
//

#ifndef FFMPEGSANDBOX_KGCODEC_H
#define FFMPEGSANDBOX_KGCODEC_H

extern "C" {
#include <libavcodec/avcodec.h>
}
class KGCodec {
  public:
      KGCodec(const AVCodec &codec, const AVCodecParameters &codecParams);
};

#endif // FFMPEGSANDBOX_KGCODEC_H
