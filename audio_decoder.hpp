//
//  audio_decoder.hpp
//  fftest
//
//  Created by 商东洲 on 2019/9/24.
//  Copyright © 2019 商东洲. All rights reserved.
//

#ifndef audio_decoder_hpp
#define audio_decoder_hpp

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
class AudioDecoder{
public:
    AudioDecoder();
    ~AudioDecoder();
    
    bool init(AVCodecID codec_id);
    bool init(AVStream *ast);
    int decode(AVPacket *packet,AVFrame *frame,int *got_frame);
    AVCodecContext *get_codec_context();
private:
    AVCodec *codec_;
    AVCodecContext *codec_ctx_;
};



#endif /* audio_decoder_hpp */
