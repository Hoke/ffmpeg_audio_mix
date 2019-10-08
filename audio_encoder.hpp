//
//  audio_encoder.hpp
//  fftest
//
//  Created by 商东洲 on 2019/9/24.
//  Copyright © 2019 商东洲. All rights reserved.
//

#ifndef audio_encoder_hpp
#define audio_encoder_hpp

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class AudioEncoder{
public:
    AudioEncoder();
    ~AudioEncoder();
    bool init(AVCodecID codec_id);
   
    int encode(AVFrame *frame,AVPacket *packet,int *got_frame);
    AVCodecContext *getCodecCtx();
private:
    AVCodec *codec_;
    AVCodecContext *codec_ctx_;
    void setCodecPar();
};

#endif /* audio_encoder_hpp */
