//
//  MediaDemuxer.hpp
//  fftest
//
//  Created by 商东洲 on 2019/9/25.
//  Copyright © 2019 商东洲. All rights reserved.
//

#ifndef MediaDemuxer_hpp
#define MediaDemuxer_hpp

extern "C"{
#include <libavformat/avformat.h>

}

class MediaDemuxer{
public:
    MediaDemuxer();
    ~MediaDemuxer();
     bool init(const char *path);
    void uninit();
    int get_track_count();
    int seek_to(long timeUs, int mode);
    int read_fream(AVPacket *pkt);
    void select_track(int index);
    void unselect_track(int index);
    int64_t get_duration() const;
    bool is_interrupted();
    bool is_audio_stream(uint32_t stream_index);
    bool is_video_stream(uint32_t stream_index);
    AVCodecID get_video_codec_id();
    AVCodecID get_audio_codec_id();
    AVStream *get_video_stream();
    AVStream *get_audio_stream();
    bool has_video() const{
        return has_video_;
    }
    bool has_audio() const{
        return has_audio_;
    }
    AVRational get_frame_rate();
    int get_video_rotate_degree();
private:
    static int check_interrupt(void *self);
    double get_rotation(AVStream *st);
    bool interrupted_;
    int64_t media_duration;
    AVFormatContext *fmt_ctx_;
    AVStream *audio_stream;
    AVStream *video_stream;
    int32_t audio_stream_index;
    int32_t video_stream_index;
    AVRational frame_rate_;
    bool has_video_;
    bool has_audio_;
};

#endif /* MediaDemuxer_hpp */
