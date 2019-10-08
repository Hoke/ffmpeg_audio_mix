//
//  music_mix.cpp
//  fftest
//
//  Created by 商东洲 on 2019/9/18.
//  Copyright © 2019 商东洲. All rights reserved.
//


extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
    
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avutil.h"
#include "libavutil/fifo.h"
}


#include <time.h>
#include <pthread.h>

#include <iostream>
using namespace std;
#include <unistd.h>
#include "audio_decoder.hpp"
#include "audio_encoder.hpp"
#include "MediaDemuxer.hpp"
#include "media_muxer.hpp"



enum CaptureState
{
    PREPARED,
    RUNNING,
    STOPPED,
    FINISHED
};




AVFilterGraph *_filter_graph=NULL;
AVFilterContext *_filter_ctx_src_spk=NULL;
AVFilterContext *_filter_ctx_src_mic=NULL;
AVFilterContext *_filter_ctx_sink=NULL;

CaptureState _state=CaptureState::PREPARED;

AVAudioFifo *_fifo_spk=NULL;
AVAudioFifo *_fifo_mic=NULL;
AVAudioFifo *_fifo_out_=NULL;

pthread_mutex_t _speaker_mutex;
pthread_mutex_t _microphone_mutex;
pthread_cond_t _speaker_cond;
pthread_cond_t _microphone_cond;

pthread_t g_thread[2] ;

AudioEncoder *audio_encoder_;
AudioDecoder *audio_decoder1_;
AudioDecoder *audio_decoder2_;
MediaMuxer *media_muxer_;
MediaDemuxer *media_demuxer1_;
MediaDemuxer *media_demuxer2_;
int64_t main_duration=0;
int main_index=1;





void initRecorder(){
    av_register_all();
    avdevice_register_all();
    avfilter_register_all();
    pthread_mutex_init(&_speaker_mutex, NULL);
    pthread_mutex_init(&_microphone_mutex, NULL);
    pthread_cond_init(&_speaker_cond, NULL);
    pthread_cond_init(&_microphone_cond, NULL);
}

void flush_audio_encoder(int64_t frame_count) {
AVPacket pkt_flush;
pkt_flush.data = NULL;
pkt_flush.size = 0;
int got_frame = 0;
av_init_packet(&pkt_flush);

int ret = audio_encoder_->encode(NULL, &pkt_flush, &got_frame);
    
    if (ret >= 0 && got_frame) {
        pkt_flush.stream_index=0;
        pkt_flush.pts=frame_count*media_muxer_->get_audio_stream()->codecpar->frame_size;
        pkt_flush.dts=pkt_flush.pts;
        pkt_flush.duration=media_muxer_->get_audio_stream()->codec->frame_size;
        
        pkt_flush.pts = av_rescale_q_rnd(pkt_flush.pts,
                                         media_muxer_->get_audio_stream()->time_base,
                                         media_muxer_->get_audio_stream()->time_base,
                                         (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt_flush.dts = pkt_flush.pts;
        pkt_flush.duration = av_rescale_q_rnd(pkt_flush.duration,
                                                                 media_muxer_->get_audio_stream()->codec->time_base,
                                                                 media_muxer_->get_audio_stream()->time_base,
                                                                 (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        
        media_muxer_->write_frame(&pkt_flush);
    }
}


    
int open_spearker_input(char *input_format,char *url){
    media_demuxer1_=new MediaDemuxer();
    if(!(media_demuxer1_->init(url))){
        printf("speaker:failed to init url");
        return -1;
    }
    audio_decoder1_=new AudioDecoder();
    if(!(audio_decoder1_->init(media_demuxer1_->get_audio_stream()))){
        printf("speaker:failed to init decoder");
        return -1;
    }
    
    return 0;
}
    

int open_microphone_input(char *input_format,char *url){
    media_demuxer2_=new MediaDemuxer();
    if (!(media_demuxer2_->init(url))) {
         printf("mic:failed to init url");
               return -1;
    }
    audio_decoder2_=new AudioDecoder();
    if(!(audio_decoder2_->init(media_demuxer2_->get_audio_stream()))){
        printf("mic:failed to init decoder");
        return -1;
    }
    return 0;
   
}

int open_file_output(char *file_name){
    media_muxer_=new MediaMuxer();
    media_muxer_->init(file_name);
   
    
    audio_encoder_=new AudioEncoder();
    audio_encoder_->init(AV_CODEC_ID_AAC);
    
    media_muxer_->add_audio_stream(audio_encoder_);
    media_muxer_->write_header(file_name);
    
    
    
    _fifo_spk=av_audio_fifo_alloc(media_demuxer1_->get_audio_stream()->codec->sample_fmt
                                  , media_demuxer1_->get_audio_stream()->codec->channels
                                  ,30*media_demuxer1_->get_audio_stream()->codec->frame_size);
    _fifo_mic=av_audio_fifo_alloc(media_demuxer2_->get_audio_stream()->codec->sample_fmt
                                  , media_demuxer2_->get_audio_stream()->codec->channels
                                  , 30*media_demuxer2_->get_audio_stream()->codec->frame_size);
    
    _fifo_out_=av_audio_fifo_alloc(media_demuxer2_->get_audio_stream()->codec->sample_fmt
                                    , media_demuxer2_->get_audio_stream()->codec->channels
                                   , 30*media_demuxer2_->get_audio_stream()->codec->frame_size);
    
    return 0;
    
}

int init_filter(char* filter_desc){
    char args_spk[512];
    char *pad_name_spk="0:a";
    char args_mic[512];
    char *pad_name_mic="1:a";
    
    const AVFilter *filter_src_spk = avfilter_get_by_name("abuffer");
    const AVFilter *filter_src_mic=avfilter_get_by_name("abuffer");
    const AVFilter *filter_sink=avfilter_get_by_name("abuffersink");
    
    AVFilterInOut *filter_output_spk=avfilter_inout_alloc();
    AVFilterInOut *filter_output_mic=avfilter_inout_alloc();
    AVFilterInOut *filter_input=avfilter_inout_alloc();
    _filter_graph=avfilter_graph_alloc();
    
    snprintf(args_spk,sizeof(args_spk)
             ,"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%u"
             ,media_demuxer1_->get_audio_stream()->codec->time_base.num
             ,media_demuxer1_->get_audio_stream()->codec->time_base.den
             ,media_demuxer1_->get_audio_stream()->codec->sample_rate
             ,av_get_sample_fmt_name(media_demuxer1_->get_audio_stream()->codec->sample_fmt)
             ,media_demuxer1_->get_audio_stream()->codec->channel_layout);
    
    snprintf(args_mic, sizeof(args_mic)
             ,"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%u"
             ,media_demuxer2_->get_audio_stream()->codec->time_base.num,
             media_demuxer2_->get_audio_stream()->codec->time_base.den,
             media_demuxer2_->get_audio_stream()->codec->sample_rate,
             av_get_sample_fmt_name(media_demuxer2_->get_audio_stream()->codec->sample_fmt),
             media_demuxer2_->get_audio_stream()->codec->channel_layout);
    
    int ret=0;
    ret=avfilter_graph_create_filter(&_filter_ctx_src_spk, filter_src_spk, pad_name_spk
                                     , args_spk, NULL, _filter_graph);
    if (ret<0) {
        printf("Filter:failed to call avfilter_graph_create_filter --src spk\n");
        return -1;
    }
    ret=avfilter_graph_create_filter(&_filter_ctx_src_mic, filter_src_mic, pad_name_mic
                                     , args_mic, NULL, _filter_graph);
    if (ret<0) {
        printf("Filter:failed to call avfilter_graph_create_filter --src mic\n");
        return -1;
    }
    
    ret=avfilter_graph_create_filter(&_filter_ctx_sink, filter_sink, "out", NULL
                                     , NULL,_filter_graph);
    if (ret<0) {
        printf("Filter:failed to call avfilter_graph_create_filer --sinl\n");
        return -1;
    }
   
    AVCodecContext *encodec_ctx=audio_encoder_->getCodecCtx();
    ret=av_opt_set_bin(_filter_ctx_sink, "sample_fmts"
                       , (uint8_t*)&encodec_ctx->sample_fmt
                       , sizeof(encodec_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("Filter: failed to call av_opt_set_bin -- sample_fmts\n");
        return -1;
    }
    ret = av_opt_set_bin(_filter_ctx_sink, "channel_layouts", (uint8_t*)&encodec_ctx->channel_layout, sizeof(encodec_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("Filter: failed to call av_opt_set_bin -- channel_layouts\n");
        return -1;
    }
    ret = av_opt_set_bin(_filter_ctx_sink, "sample_rates", (uint8_t*)&encodec_ctx->sample_rate, sizeof(encodec_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        printf("Filter: failed to call av_opt_set_bin -- sample_rates\n");
        return -1;
    }
    filter_output_spk->name=av_strdup(pad_name_spk);
    filter_output_spk->filter_ctx=_filter_ctx_src_spk;
    filter_output_spk->pad_idx=0;
    filter_output_spk->next=filter_output_mic;
    
    filter_output_mic->name=av_strdup(pad_name_mic);
    filter_output_mic->filter_ctx=_filter_ctx_src_mic;
    filter_output_mic->pad_idx=0;
    filter_output_mic->next=NULL;
    
    filter_input->name = av_strdup("out");
    filter_input->filter_ctx = _filter_ctx_sink;
    filter_input->pad_idx = 0;
    filter_input->next = NULL;
    
    
    AVFilterInOut *filter_outputs[2];
    filter_outputs[0]=filter_output_spk;
    filter_outputs[1]=filter_output_mic;
    
    ret=avfilter_graph_parse_ptr(_filter_graph, filter_desc, &filter_input
                                 , filter_outputs, NULL);
    
    if (ret < 0)
    {
        printf("Filter: failed to call avfilter_graph_parse_ptr\n");
        return -1;
    }
    
    ret=avfilter_graph_config(_filter_graph, NULL);
    if (ret < 0)
    {
        printf("Filter: failed to call avfilter_graph_config\n");
        return -1;
    }
    avfilter_inout_free(&filter_input);
    avfilter_inout_free(filter_outputs);
    char* temp = avfilter_graph_dump(_filter_graph, NULL);
    printf("%s\n", temp);
    return 0;
    
}

void *speaker_cap_thread_proc(void *args){
    AVFrame *pFrame=av_frame_alloc();
    AVPacket packet;
    av_init_packet(&packet);
    bool isMain=main_index==0;
    
    int got_sound;
    
    while (_state==CaptureState::RUNNING) {
        packet.data=NULL;
        packet.size=0;
        int ret=media_demuxer1_->read_fream(&packet);
        if (ret<0) {
            printf("thread:speaker ret:%d\n",ret);
            if(ret==AVERROR_EOF){
                if (!isMain) {
                    media_demuxer1_->seek_to(0, 0);
                }else{
                    _state=CaptureState::STOPPED;
                }
            }
            usleep(10000);
            continue;
        }
        if (media_demuxer1_->is_audio_stream(packet.stream_index)) {
            if (audio_decoder1_->decode(&packet, pFrame, &got_sound)<0) {
                break;
            }
            av_packet_unref(&packet);
            if (!got_sound) {
                continue;
            }
            
            int fifo_spk_space=av_audio_fifo_space(_fifo_spk);
            while (fifo_spk_space<pFrame->nb_samples&&_state==CaptureState::RUNNING) {
                usleep(10000);
                printf("fifo_spk full \n");
                
                fifo_spk_space=av_audio_fifo_space(_fifo_spk);
                
            }
            if(fifo_spk_space>=pFrame->nb_samples){
                pthread_mutex_lock(&_speaker_mutex);
                int nWritten=av_audio_fifo_write(_fifo_spk,  (void**)pFrame->data, pFrame->nb_samples);
                pthread_mutex_unlock(&_speaker_mutex);
                
            }
        }
       
    }
    av_frame_free(&pFrame);
    return 0;
    
}

void *microphone_cap_thread_proc(void *args){
    AVFrame *pFrame=av_frame_alloc();
    AVPacket packet;
    av_init_packet(&packet);
     bool isMain=main_index==1;
    
    int got_sound;
    while (_state==CaptureState::PREPARED) {
         usleep(10000);
    }
    while (_state==CaptureState::RUNNING) {
        packet.data=NULL;
        packet.size=0;
        int ret=media_demuxer2_->read_fream(&packet);
        if (ret<0) {
             printf("thread:mic ret:%d\n",ret);
            usleep(10000);
            if (!isMain) {
                media_demuxer2_->seek_to(0, 0);
            }else{
                _state=CaptureState::STOPPED;
            }
            continue;
        }
        if (media_demuxer2_->is_audio_stream(packet.stream_index)) {
            if (audio_decoder2_->decode(&packet, pFrame, &got_sound)<0) {
                usleep(10000);
                continue;
            }
            av_packet_unref(&packet);
            int fifo_mic_space=av_audio_fifo_space(_fifo_mic);
            while (fifo_mic_space<pFrame->nb_samples&&_state==CaptureState::RUNNING) {
                usleep(10000);
                printf("fifo mic full\n");
                fifo_mic_space=av_audio_fifo_space(_fifo_mic);
            }
            if (fifo_mic_space>=pFrame->nb_samples) {
                pthread_mutex_lock(&_microphone_mutex);
                av_audio_fifo_write(_fifo_mic, (void**)pFrame->data, pFrame->nb_samples);
                pthread_mutex_unlock(&_microphone_mutex);
            }
                       
        }
    }
    av_frame_free(&pFrame);
    return 0;
}

void relase(){
    
    pthread_mutex_destroy( &_speaker_mutex ) ;
    pthread_mutex_destroy(&_microphone_mutex);
    
    pthread_cond_destroy(&_speaker_cond);
    pthread_cond_destroy(&_microphone_cond);
    
    av_audio_fifo_free(_fifo_spk);
    av_audio_fifo_free(_fifo_mic);
    
    avfilter_free(_filter_ctx_src_spk);
    avfilter_free(_filter_ctx_src_mic);
    avfilter_free(_filter_ctx_sink);
    
    avfilter_graph_free(&_filter_graph);
    
    
    if (media_demuxer1_) {
           media_demuxer1_->uninit();
           delete media_demuxer1_;
           media_demuxer1_ = NULL;
       }
    if (media_demuxer2_) {
              media_demuxer2_->uninit();
              delete media_demuxer2_;
              media_demuxer2_ = NULL;
          }

       
       if (audio_decoder1_) {
           delete audio_decoder1_;
           audio_decoder1_ = NULL;
       }
    if (audio_decoder2_) {
              delete audio_decoder2_;
              audio_decoder2_ = NULL;
          }

      
      

       if (audio_encoder_) {
           delete audio_encoder_;
           audio_encoder_ = NULL;
       }

       if (media_muxer_) {
           delete media_muxer_;
           media_muxer_ = NULL;
       }

     
    
}

int main(){
    int ret =0;
    initRecorder();
    
    char file_name[128];
    char *out_type=".mp3";
    time_t rawtime;
    int speaker_count=0;
    int micro_count=0;
    
    int64_t duration[2]={0,0};
    int64_t cur_time=0;
    
    tm *timeinfo;
    time(&rawtime);
    timeinfo=localtime(&rawtime);
    snprintf(file_name, sizeof(file_name), "%d_%d_%d_%d_%d_%d%s",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, out_type);
    //char *filter_desc="[in0][in1]amix=inputs=2[out]";
    
    char *filter_desc="[0:a]volume=1.0[a1];[1:a]volume=2.1[a2];[a1][a2]amix=inputs=2[out]";
    ret=open_spearker_input(NULL, "/foxy/111171e2b22-5332-4e1d-8ba9-ae602a1f2b11.m4a");
    if (ret < 0)
    {
        relase();
        return ret;
    }
    
    ret=open_microphone_input(NULL, "/Users/shangdongzhou/Downloads/video.mp4");
    if (ret<0) {
        relase();
        return ret;
    }
    
    ret = open_file_output("/foxy/test.m4a");
    if (ret < 0)
    {
        relase();
        return ret;
    }
    
    ret=init_filter(filter_desc);
    if(ret<0){
        relase();
        return ret;
    }
    _state=CaptureState::RUNNING;
    
    pthread_create(&g_thread[0], NULL, speaker_cap_thread_proc, NULL);
    pthread_create(&g_thread[1], NULL, microphone_cap_thread_proc, NULL);
    if (main_index==0) {
        main_duration=media_demuxer1_->get_duration();
    }else{
        main_duration=media_demuxer2_->get_duration();
    }
    
    
    
    
    
    
    int tmp_fifo_failed=0;
    int64_t frame_count=0;
    while (_state==CaptureState::RUNNING) {
        
        int ret =0;
        AVFrame *pFrame_spk=av_frame_alloc();
        AVFrame *pFrame_mic=av_frame_alloc();
        AVPacket packet_out;
        int got_packet_ptr;
        int fifo_spk_size=av_audio_fifo_size(_fifo_spk);
        int fifo_mic_size=av_audio_fifo_size(_fifo_mic);
        int frame_spk_min_size=media_demuxer1_->get_audio_stream()->codecpar->frame_size;
        int frame_mic_min_size=media_demuxer2_->get_audio_stream()->codecpar->frame_size;
        
        if(fifo_spk_size>=frame_spk_min_size||fifo_mic_size>=frame_mic_min_size){
            tmp_fifo_failed=0;
            if (fifo_spk_size>=frame_spk_min_size&&(duration[0]-cur_time)<1*1000*1000) {
                pFrame_spk->nb_samples=frame_spk_min_size;
                pFrame_spk->channel_layout=media_demuxer1_->get_audio_stream()->codec->channel_layout;
               
                pFrame_spk->format=media_demuxer1_->get_audio_stream()->codec->sample_fmt;
                pFrame_spk->sample_rate=media_demuxer1_->get_audio_stream()->codec->sample_rate;
                av_frame_get_buffer(pFrame_spk, 0);
                
                pthread_mutex_lock(&_speaker_mutex);
                ret=av_audio_fifo_read(_fifo_spk, (void**)pFrame_spk->data, frame_spk_min_size);
                pthread_mutex_unlock(&_speaker_mutex);
                
                pFrame_spk->pts=speaker_count*frame_spk_min_size;
                
                
                
                duration[0]= pFrame_spk->pts*av_q2d(media_demuxer1_->get_audio_stream()->codec->time_base)*1000*1000;
                
                speaker_count++;
                ret=av_buffersrc_add_frame(_filter_ctx_src_spk, pFrame_spk);
                
                if (ret<0) {
                    printf("Mixer:failed to call av_buffersrc_add_frame(speaker)\n");
                    break;
                }
            }
            
            if (fifo_mic_size>=frame_mic_min_size&&(duration[1]-cur_time)<1*1000*1000) {
                pFrame_mic->nb_samples=frame_mic_min_size;
                pFrame_mic->channel_layout=media_demuxer2_->get_audio_stream()->codec->channel_layout;
                pFrame_mic->format=media_demuxer2_->get_audio_stream()->codec->sample_fmt;
                pFrame_mic->sample_rate=media_demuxer2_->get_audio_stream()->codec->sample_rate;
                av_frame_get_buffer(pFrame_mic, 0);
                
                pthread_mutex_lock(&_microphone_mutex);
                ret=av_audio_fifo_read(_fifo_mic, (void**)pFrame_mic->data, frame_mic_min_size);
                pthread_mutex_unlock(&_microphone_mutex);
                
                
                pFrame_mic->pts=micro_count*frame_mic_min_size;
                duration[1]= pFrame_mic->pts*av_q2d(media_demuxer2_->get_audio_stream()->codec->time_base)*1000*1000;
                
                
                micro_count++;
                
                
                
                ret=av_buffersrc_add_frame(_filter_ctx_src_mic, pFrame_mic);
                if (ret<0) {
                    printf("Mixter:failed to call av_framersrc_add_frame(mic)\n");
                    break;
                }
            }
            
            
            while (1) {
                AVFrame *pFrame_out=av_frame_alloc();
                ret=av_buffersink_get_frame_flags(_filter_ctx_sink, pFrame_out, 0);
                if (ret<0) {
                    //usleep(10000);
                    printf("Mixer:failed to call av_buffersink_get_frame_flags\n");
                    break;
                }
                
                if (pFrame_out->data[0]!=NULL) {
                    av_init_packet(&packet_out);
                    packet_out.data=NULL;
                    packet_out.size=0;
                    
                    ret=audio_encoder_->encode(pFrame_out, &packet_out, &got_packet_ptr);
                    
                  
                    if (ret<0) {
                        printf("Mixer:failed to call avcodec_encode_audio2\n");
                        break;
                    }
                    if (got_packet_ptr) {
                        packet_out.stream_index=0;
                        packet_out.pts=frame_count*media_muxer_->get_audio_stream()->codecpar->frame_size;
                        packet_out.dts=packet_out.pts;
                        packet_out.duration=media_muxer_->get_audio_stream()->codec->frame_size;
                        
                        packet_out.pts = av_rescale_q_rnd(packet_out.pts,
                                                          media_muxer_->get_audio_stream()->time_base,
                                                          media_muxer_->get_audio_stream()->time_base,
                                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        packet_out.dts = packet_out.pts;
                        packet_out.duration = av_rescale_q_rnd(packet_out.duration,
                                                               media_muxer_->get_audio_stream()->codec->time_base,
                                                               media_muxer_->get_audio_stream()->time_base,
                                                               (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        cur_time=packet_out.pts*av_q2d(media_muxer_->get_audio_stream()->time_base)*1000*1000;
                        frame_count++;
                        ret=media_muxer_->write_frame(&packet_out);
                        if (cur_time>=main_duration) {
                            _state=CaptureState::STOPPED;
                        }
                        
                        if (ret<0) {
                            printf("Mixer:failed to call av_interleaved_write_frame\n");
                            
                        }
                        printf("Mixer:write frame to file\n");
                        
                    }
                    av_free_packet(&packet_out);
                }
                av_frame_free(&pFrame_out);
            }
        }
        else{
            tmp_fifo_failed++;
            usleep(10000);
            if (tmp_fifo_failed>10) {
                _state=CaptureState::STOPPED;
                usleep(30000);
                break;
            }
            
            av_frame_free(&pFrame_spk);
            av_frame_free(&pFrame_mic);
            
            
            
        }
        
    }
    
    flush_audio_encoder(frame_count);
    media_muxer_->write_tailer();
    relase();
   
    
    return ret;
}






