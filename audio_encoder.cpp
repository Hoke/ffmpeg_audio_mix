//
//  audio_encoder.cpp
//  fftest
//
//  Created by 商东洲 on 2019/9/24.
//  Copyright © 2019 商东洲. All rights reserved.
//

#include "audio_encoder.hpp"

AudioEncoder::AudioEncoder():codec_(NULL),codec_ctx_(NULL){
    
}

AudioEncoder::~AudioEncoder(){
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    
}

bool AudioEncoder::init(AVCodecID codec_id){
    codec_=avcodec_find_encoder(codec_id);
    if (!codec_) {
        return false;
    }
    codec_ctx_=avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        return false;
    }
    setCodecPar();
    if (avcodec_open2(codec_ctx_, codec_, NULL)<0) {
        return false;
    }
    return true;
}


void AudioEncoder::setCodecPar(){
    codec_ctx_->codec_type=AVMEDIA_TYPE_AUDIO;
    codec_ctx_->codec=codec_;
    codec_ctx_->sample_rate=44100;
    codec_ctx_->channels=2;
    codec_ctx_->channel_layout=av_get_default_channel_layout(2);
    codec_ctx_->sample_fmt=codec_->sample_fmts[0];
    codec_ctx_->bit_rate=128000;
    codec_ctx_->time_base.num=1;
    codec_ctx_->time_base.den=codec_ctx_->sample_rate;
    codec_ctx_->codec_tag=0;
    
}

int AudioEncoder::encode(AVFrame *frame,AVPacket *packet,int *got_frame){
    int ret=avcodec_send_frame(codec_ctx_, frame);
    if (ret<0) {
        return ret;
    }
    ret=avcodec_receive_packet(codec_ctx_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    } else if (ret < 0) {
        return ret;
    } else {
        *got_frame = 1;
    }

    return ret;
}




