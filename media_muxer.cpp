//
//  media_muxer.cpp
//  fftest
//
//  Created by 商东洲 on 2019/9/25.
//  Copyright © 2019 商东洲. All rights reserved.
//

#include "media_muxer.hpp"
class MediaDemuxer;
MediaMuxer::MediaMuxer():audio_stream_(NULL),video_stream_(NULL),fmt_ctx_(NULL){
}

MediaMuxer::MediaMuxer(MediaDemuxer *demuxer): audio_stream_(NULL), video_stream_(NULL),
fmt_ctx_(NULL), media_demuxer_(demuxer), is_writed_header_(false){
    
}
MediaMuxer::~MediaMuxer(){
    release();
}

bool MediaMuxer::init(const char *file_name){
    avformat_alloc_output_context2(&fmt_ctx_, NULL, NULL, file_name);
    if (!fmt_ctx_) {
        avformat_alloc_output_context2(&fmt_ctx_, NULL, "mp4", file_name);
    }
    if (!fmt_ctx_) {
        return false;
    }
    return true;

}

void MediaMuxer::write_tailer(){
    if (fmt_ctx_) {
        av_write_trailer(fmt_ctx_);
    }
}

int  MediaMuxer::write_frame(AVPacket *pkt){
    return av_interleaved_write_frame(fmt_ctx_, pkt);
}

void MediaMuxer::release(){
    if (fmt_ctx_) {
        if (!(fmt_ctx_->oformat->flags&AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx_->pb);
        }
        avformat_free_context(fmt_ctx_);
        fmt_ctx_=NULL;
    }
}

int MediaMuxer::write_frame(const AVRational *time_base,AVStream *st,AVPacket *pkt){
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index=st->index;
    return av_interleaved_write_frame(fmt_ctx_, pkt);
}

void MediaMuxer::add_stream(AVFormatContext *fmt_ctx, AVCodecID codec_id){
    AVStream *st=avformat_new_stream(fmt_ctx, NULL);
    st->id=fmt_ctx->nb_streams-1;
    if (codec_id==AV_CODEC_ID_AAC) {
        AVStream *ast=media_demuxer_->get_audio_stream();
        st->time_base.num = ast->time_base.num;
        st->time_base.den = ast->time_base.den;
        st->codecpar->sample_rate = ast->codecpar->sample_rate;
        st->codecpar->codec_id = AV_CODEC_ID_AAC;
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_stream_ = st;
    } else {
            AVStream *vst = media_demuxer_->get_video_stream();
            st->time_base.num = vst->time_base.num;
            st->time_base.den = vst->time_base.den;
            st->codecpar->codec_id = AV_CODEC_ID_H264;
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codecpar->width = vst->codecpar->width;
            st->codecpar->height = vst->codecpar->height;
            video_stream_ = st;
            av_dict_copy(&video_stream_->metadata, vst->metadata, 0);
        }
}
      
    
int32_t MediaMuxer::add_audio_stream(AVCodecContext *ctx) {
       audio_stream_ = avformat_new_stream(fmt_ctx_, NULL);
       audio_stream_->id = fmt_ctx_->nb_streams - 1;
       audio_stream_->time_base = ctx->time_base;
       int ret = avcodec_parameters_from_context(audio_stream_->codecpar, ctx);
       if (ret < 0) {
           return -1;
       } else {
           return audio_stream_->id;
       }
   }

int32_t MediaMuxer::add_video_stream(AVCodecContext *ctx, bool rotated) {
    codec_ctx_ = ctx;
    video_stream_ = avformat_new_stream(fmt_ctx_, NULL);
    video_stream_->id = fmt_ctx_->nb_streams - 1;
    video_stream_->time_base = ctx->time_base;

    if (rotated) {
        av_dict_set(&video_stream_->metadata, "rotate", NULL, 0);
    } else {
        AVStream *vst = media_demuxer_->get_video_stream();
        av_dict_copy(&video_stream_->metadata, vst->metadata, 0);
        if (vst->nb_side_data) {
            for (int i = 0; i < vst->nb_side_data; i++) {
                const AVPacketSideData *sd_src = &vst->side_data[i];
                uint8_t *dst_data;

                dst_data = av_stream_new_side_data(video_stream_, sd_src->type, sd_src->size);
                if (!dst_data) {
                    return false;
                }
                memcpy(dst_data, sd_src->data, sd_src->size);
            }
        }

        {
            AVDictionaryEntry *tag = NULL;
            while ((tag = av_dict_get(video_stream_->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
               // LOGI("muxer video stream %s=%s\n", tag->key, tag->value);
            }
        }
    }
    int ret = avcodec_parameters_from_context(video_stream_->codecpar, ctx);
    if (ret < 0) {
        return -1;
    } else {
        return video_stream_->id;
    }
}

bool MediaMuxer::write_header(const char *filename) {
    // avcodec_parameters_from_context(video_stream_->codecpar, ctx_);
    AVDictionary *opt = NULL;
    int ret;
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx_->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            return false;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmt_ctx_, &opt);
    if (ret < 0) {
        return false;
    }

    is_writed_header_ = true;
    return true;
}

AVStream *MediaMuxer::get_stream(int index) {
    if (index == 0) {
        return video_stream_;
    } else {
        return audio_stream_;
    }
}


