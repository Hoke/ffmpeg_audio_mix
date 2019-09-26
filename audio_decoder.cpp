//
//  audio_decoder.cpp
//  fftest
//
//  Created by 商东洲 on 2019/9/24.
//  Copyright © 2019 商东洲. All rights reserved.
//

#include "audio_decoder.hpp"
AudioDecoder::AudioDecoder():codec_(NULL),codec_ctx_(NULL){
    
}

AudioDecoder::~AudioDecoder(){
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
}

bool AudioDecoder::init(AVCodecID codec_id){
    codec_=avcodec_find_decoder(codec_id);
    if (!codec_) {
        return false;
    }
    codec_ctx_=avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        return false;
    }
    if (avcodec_open2(codec_ctx_, codec_, NULL)<0) {
        return false;
    }
    return true;
}

bool AudioDecoder::init(AVStream *ast){
    int ret;
    codec_=avcodec_find_decoder(ast->codecpar->codec_id);
    if (!codec_) {
        return false;
    }
    codec_ctx_=avcodec_alloc_context3(codec_);
    if (!codec_ctx_) {
        return false;
    }
    ret=avcodec_parameters_to_context(codec_ctx_, ast->codecpar);
    if (ret<0) {
        return false;
    }
    return true;
}

int AudioDecoder::decode(AVPacket *packet,AVFrame *frame,int *got_frame){
    int ret;
    ret=avcodec_send_packet(codec_ctx_, packet);
    if (ret<0) {
        return ret;
    }
    ret=avcodec_receive_frame(codec_ctx_, frame);
    if (ret==AVERROR(EAGAIN)||ret==AVERROR_EOF) {
        return 0;
    }else if (ret<0) {
        return ret;
    }else{
        *got_frame=1;
    }
    return ret;
    
}

