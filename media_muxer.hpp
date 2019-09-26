//
//  media_muxer.hpp
//  fftest
//
//  Created by 商东洲 on 2019/9/25.
//  Copyright © 2019 商东洲. All rights reserved.
//

#ifndef media_muxer_hpp
#define media_muxer_hpp

extern "C"{
#include <libavformat/avformat.h>
}
#include "MediaDemuxer.hpp"

class MediaDemuxer;
class MediaMuxer{
public:
    MediaMuxer();
    MediaMuxer(MediaDemuxer *demuxer);
    ~MediaMuxer();
    bool init(const char *file_name);
    void write_tailer();
    int write_frame(AVPacket *pkt);
    int32_t add_audio_stream(AVCodecContext *ctx);
    int32_t add_video_stream(AVCodecContext *ctx,bool rotated);
    void set_video_stream_proterty(const char *key,const char *value);
    bool write_header(const char *file_name);
    AVStream *get_stream(int index);
    AVStream *get_audio_stream() const {
        return audio_stream_;
    }
    AVStream *get_video_stream() const{
        return video_stream_;
    }
    bool get_write_hearder_state(){
        return is_writed_header_;
    }
    
    void release();
private:
    void add_stream(AVFormatContext *fmt_ctx,AVCodecID codec_id);
    int write_frame(const AVRational *time_base,AVStream *st,AVPacket *pkt);
    AVStream *audio_stream_;
    AVStream *video_stream_;
    AVFormatContext *fmt_ctx_;
    MediaDemuxer *media_demuxer_;
    bool is_writed_header_;
    AVCodecContext *codec_ctx_;
    
    
    
};


#endif /* media_muxer_hpp */
