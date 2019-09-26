//
//  MediaDemuxer.cpp
//  fftest
//
//  Created by 商东洲 on 2019/9/25.
//  Copyright © 2019 商东洲. All rights reserved.
//

#include "MediaDemuxer.hpp"

extern "C"{
#include <libavutil/eval.h>
#include <libavutil/display.h>
}

MediaDemuxer::MediaDemuxer():interrupted_(false),fmt_ctx_(NULL),media_duration(0)
        ,audio_stream(NULL),video_stream(NULL),audio_stream_index(-1),video_stream_index(-1),
        has_audio_(false),has_video_(false){
    
}

MediaDemuxer::~MediaDemuxer(){
    uninit();
}
void MediaDemuxer::uninit() {
    if (fmt_ctx_) {
        interrupted_ = true;
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = NULL;
    }
}

bool MediaDemuxer::init(const char *url){
    if (url==NULL) {
        return false;
    }
    fmt_ctx_=avformat_alloc_context();
    fmt_ctx_->interrupt_callback.callback=MediaDemuxer::check_interrupt;
    fmt_ctx_->interrupt_callback.opaque=(void*)this;
    if (avformat_open_input(&fmt_ctx_, url, NULL, NULL)<0) {
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx_, NULL)<0){
        return false;
    }
    int ret;
    
    ret=av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret<0) {
         printf("failed to find video stream\n");
    }else{
        video_stream_index=ret;
        video_stream=fmt_ctx_->streams[video_stream_index];
        frame_rate_=av_guess_frame_rate(fmt_ctx_, video_stream, NULL);
        has_video_=true;
    }
    
    ret=av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret<0) {
        printf("failed to find audio stream\n");
    }else{
        audio_stream_index=ret;
        audio_stream=fmt_ctx_->streams[audio_stream_index];
        has_audio_=true;
    }
    media_duration=fmt_ctx_->duration;
    return true;
}

int MediaDemuxer::get_track_count(){
    return fmt_ctx_->nb_streams;
}

int MediaDemuxer::seek_to(long timeUs, int mode){
    return avformat_seek_file(fmt_ctx_, -1, INT64_MIN, timeUs, INT64_MAX, 0);
}

int MediaDemuxer::read_fream(AVPacket *pkt){
    return av_read_frame(fmt_ctx_, pkt);
}

bool MediaDemuxer::is_interrupted(){
    return interrupted_;
}

int MediaDemuxer::check_interrupt(void *self){
    MediaDemuxer *mediaDemuxer=(MediaDemuxer *)self;
    if (mediaDemuxer->is_interrupted()) {
        return 1;
    }else{
        return 0;
    }
    
}

bool MediaDemuxer::is_audio_stream(uint32_t stream_index){
    if (audio_stream_index==stream_index) {
        return true;
        
    }else{
        return false;
    }
}

bool MediaDemuxer::is_video_stream(uint32_t stream_index){
    return video_stream_index==stream_index;
}

AVCodecID MediaDemuxer::get_video_codec_id(){
    int ret;
    ret=av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret<0) {
        return AV_CODEC_ID_NONE;
    }else{
        video_stream_index=ret;
        video_stream=fmt_ctx_->streams[video_stream_index];
        return video_stream->codecpar->codec_id;
    }
}

AVCodecID MediaDemuxer::get_audio_codec_id(){
    int ret;
    ret=av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret<0) {
        return AV_CODEC_ID_NONE;
    }else{
        audio_stream_index=ret;
        audio_stream=fmt_ctx_->streams[audio_stream_index];
        return audio_stream->codecpar->codec_id;
    }
}


AVStream *MediaDemuxer::get_video_stream() {

    return video_stream;
}

AVStream *MediaDemuxer::get_audio_stream() {
    return audio_stream;
}

int64_t MediaDemuxer::get_duration() const {
    return media_duration;
}

AVRational MediaDemuxer::get_frame_rate() {
    return frame_rate_;
}

int MediaDemuxer::get_video_rotate_degree() {
    int theta = abs((int) ((int64_t) round(fabs(get_rotation(video_stream))) % 360));
    switch (theta) {
        case 0:
        case 90:
        case 180:
        case 270:
            break;
        case 360:
            theta = 0;
            break;
        default:
           
            theta = 0;
            break;
    }

    return theta;
}

double MediaDemuxer::get_rotation(AVStream *st) {
    AVDictionaryEntry *rotate_tag = av_dict_get(st->metadata, "rotate", NULL, 0);
    uint8_t *displaymatrix = av_stream_get_side_data(st,
                                                     AV_PKT_DATA_DISPLAYMATRIX, NULL);
    double theta = 0;

    if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0")) {
        char *tail;
        theta = av_strtod(rotate_tag->value, &tail);
        if (*tail)
            theta = 0;
    }
    if (displaymatrix && !theta)
        theta = -av_display_rotation_get((int32_t *) displaymatrix);

    theta -= 360 * floor(theta / 360 + 0.9 / 360);

    if (fabs(theta - 90 * round(theta / 90)) > 2)
        av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
                                     "If you want to help, upload a sample "
                                     "of this file to ftp://upload.ffmpeg.org/incoming/ "
                                     "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return theta;
}
