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


AVFormatContext *_fmt_ctx_spk=NULL;
AVFormatContext *_fmt_ctx_mic=NULL;
AVFormatContext *_fmt_ctx_out=NULL;

int _index_spk=-1;
int _index_mic=-1;
int _index_a_out=-1;

AVFilterGraph *_filter_graph=NULL;
AVFilterContext *_filter_ctx_src_spk=NULL;
AVFilterContext *_filter_ctx_src_mic=NULL;
AVFilterContext *_filter_ctx_sink=NULL;

CaptureState _state=CaptureState::PREPARED;

AVAudioFifo *_fifo_spk=NULL;
AVAudioFifo *_fifo_mic=NULL;

pthread_mutex_t _speaker_mutex;
pthread_mutex_t _microphone_mutex;
pthread_cond_t _speaker_cond;
pthread_cond_t _microphone_cond;

pthread_t g_thread[2] ;
AVCodecContext *_codec_ctx_speaker;
AVCodecContext *_codec_ctx_mic;
AudioEncoder *audio_encoder_;
AudioDecoder *audio_decoder_;
MediaMuxer *media_muxer_;
MediaDemuxer *media_demuxer_;





void initRecorder(){
    av_register_all();
    avdevice_register_all();
    avfilter_register_all();
    pthread_mutex_init(&_speaker_mutex, NULL);
    pthread_mutex_init(&_microphone_mutex, NULL);
    pthread_cond_init(&_speaker_cond, NULL);
    pthread_cond_init(&_microphone_cond, NULL);
}

int open_spearker_input(char *input_format,char *url){
    
    AVInputFormat *ifmat=av_find_input_format(input_format);
    AVDictionary *opt1=NULL;
    av_dict_set(&opt1, "rtbufdize","10M" , 0);
    AVCodec *codec;
    
    
    int ret=0;
    ret=avformat_open_input(&_fmt_ctx_spk, url, ifmat, &opt1);
    if (ret<0) {
        printf("Speaker: failed to call avformat_open_input\n");
        return -1;
    }
    
    ret=avformat_find_stream_info(_fmt_ctx_spk, NULL);
    if (ret<0) {
        printf("Speaker: failed to call avformat_find_stream_info\n");
        return -1;
    }
    _index_spk=av_find_best_stream(_fmt_ctx_spk, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (_index_spk<0) {
        printf("Speaker:negative audio index\n");
        return -1;
    }
    _codec_ctx_speaker=avcodec_alloc_context3(codec);
    if (_codec_ctx_speaker==NULL) {
         printf("Speaker:avcodec_alloc_context3\n");
               return -1;
    }
    avcodec_parameters_to_context(_codec_ctx_speaker, _fmt_ctx_spk->streams[_index_spk]->codecpar);
    ret=avcodec_open2(_codec_ctx_speaker, codec, NULL);
    if (ret<0) {
        printf("Speaker:failed to call avcodec_open2\n");
        return -1;
    }
    av_dump_format(_fmt_ctx_spk, _index_spk, url, 0);
    return 0;
}



int open_microphone_input(char *input_format,char *url){
    
    AVCodec *codec;
    AVInputFormat *ifmt=av_find_input_format(input_format);
    AVDictionary *opt1=NULL;
    av_dict_set(&opt1, "rebufsize", "10M", 0);
    
    int ret=0;
    ret=avformat_open_input(&_fmt_ctx_mic, url, ifmt, &opt1);
    if (ret<0) {
        printf("Microphone:failed to call avformat_open_input\n");
        return -1;
    }
    ret=avformat_find_stream_info(_fmt_ctx_mic, NULL);
    if (ret<0) {
        printf("Microphone:failed to call avformat_find_stream_info\n");
        return -1;
        
    }
    _index_mic=av_find_best_stream(_fmt_ctx_mic, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    
    if (_index_mic<0) {
        printf("MicroPhone:negative audio index\n");
        return -1;
    }
    _codec_ctx_mic=avcodec_alloc_context3(codec);
    
    if (_codec_ctx_mic==NULL) {
         printf("mic:avcodec_alloc_context3\n");
               return -1;
    }
    
    avcodec_parameters_to_context(_codec_ctx_mic, _fmt_ctx_mic->streams[_index_mic]->codecpar);

    ret=avcodec_open2(_codec_ctx_mic, codec, NULL);
    if (ret<0) {
        printf("Microphone:failed to call avcodec_open2\n");
        return -1;
        
    }
    av_dump_format(_fmt_ctx_mic, _index_mic, url, 0);
    return 0;
}

int open_file_output(char *file_name){
    int ret=0;
    ret=avformat_alloc_output_context2(&_fmt_ctx_out, NULL, NULL, file_name);
    if (ret<0) {
        printf("Mixer:failed to call avformat_alloc_output_context2\n");
        return -1;
        
    }
    AVStream *stream_a=NULL;
    stream_a=avformat_new_stream(_fmt_ctx_out, NULL);
    if (stream_a==NULL) {
        printf("Mixer:failed to call avformat_new_stream\n<#const char *, ...#>");
        return -1;
    }
    _index_a_out=0;
    
    stream_a->codec->codec_type=AVMEDIA_TYPE_AUDIO;
    AVCodec *codec_mp3=avcodec_find_encoder(AV_CODEC_ID_AAC);
    stream_a->codec->codec=codec_mp3;
    stream_a->codec->sample_rate=44100;
    stream_a->codec->channels=2;
    stream_a->codec->channel_layout=av_get_default_channel_layout(2);
    stream_a->codec->sample_fmt=codec_mp3->sample_fmts[0];
    stream_a->codec->bit_rate=128000;
    stream_a->codec->time_base.num=1;
    stream_a->codec->time_base.den=stream_a->codec->sample_rate;
    stream_a->codec->codec_tag=0;
    
    
    if (_fmt_ctx_out->oformat->flags&AVFMT_GLOBALHEADER) {
        stream_a->codec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(stream_a->codec,stream_a->codec->codec, NULL)<0) {
        printf("Mixer:failed to call avcodec_open2\n");
        return -1;
    }
    if (!(_fmt_ctx_out->oformat->flags&AVFMT_NOFILE)) {
        if (avio_open(&_fmt_ctx_out->pb, file_name, AVIO_FLAG_WRITE)<0) {
            printf("Mixer:failed to call avio_open\n");
            return -1;
        }
    }
    
    if (avformat_write_header(_fmt_ctx_out, NULL)<0) {
        printf("Mixter:fialed to call avformat_write_header\n");
        return -1;
    }
    bool b=(!_fmt_ctx_out->streams[0]->time_base.num&&_fmt_ctx_out->streams[0]->codec->time_base.num);
    av_dump_format(_fmt_ctx_out, _index_a_out, file_name, 1);
    _fifo_spk=av_audio_fifo_alloc(_fmt_ctx_spk->streams[_index_spk]->codec->sample_fmt
                                  , _fmt_ctx_spk->streams[_index_spk]->codec->channels
                                  ,30*_fmt_ctx_spk->streams[_index_spk]->codec->frame_size);
    _fifo_mic=av_audio_fifo_alloc(_fmt_ctx_mic->streams[_index_mic]->codec->sample_fmt
                                  , _fmt_ctx_mic->streams[_index_mic]->codec->channels
                                  , 30*_fmt_ctx_mic->streams[_index_mic]->codec->frame_size);
    
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
             ,_fmt_ctx_spk->streams[_index_spk]->codec->time_base.num
             ,_fmt_ctx_spk->streams[_index_spk]->codec->time_base.den
             ,_fmt_ctx_spk->streams[_index_spk]->codec->sample_rate
             ,av_get_sample_fmt_name(_fmt_ctx_spk->streams[_index_spk]->codec->sample_fmt)
             ,_fmt_ctx_spk->streams[_index_spk]->codec->channel_layout);
    
    snprintf(args_mic, sizeof(args_mic)
             ,"time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%u"
             ,_fmt_ctx_mic->streams[_index_mic]->codec->time_base.num,
             _fmt_ctx_mic->streams[_index_mic]->codec->time_base.den,
             _fmt_ctx_mic->streams[_index_mic]->codec->sample_rate,
             av_get_sample_fmt_name(_fmt_ctx_mic->streams[_index_mic]->codec->sample_fmt),
             _fmt_ctx_mic->streams[_index_mic]->codec->channel_layout);
    
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
    
    AVCodecContext *encodec_ctx=_fmt_ctx_out->streams[_index_a_out]->codec;
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
    
    int got_sound;
    
    while (_state==CaptureState::RUNNING) {
        packet.data=NULL;
        packet.size=0;
        if (av_read_frame(_fmt_ctx_spk, &packet)<0) {
            continue;
        }
        if (packet.stream_index==_index_spk) {
            if (avcodec_decode_audio4(_codec_ctx_speaker, pFrame, &got_sound, &packet)<0) {
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
    
    int got_sound;
    while (_state==CaptureState::PREPARED) {
        
    }
    while (_state==CaptureState::RUNNING) {
        packet.data=NULL;
        packet.size=0;
        if (av_read_frame(_fmt_ctx_mic, &packet)) {
            continue;
        }
        if (packet.stream_index==_index_mic) {
            if (avcodec_decode_audio4(_codec_ctx_mic, pFrame, &got_sound, &packet)<0) {
                continue;
            }
            av_packet_unref(&packet);
            if (!got_sound) {
                continue;
            }
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
    
    
    if (_fmt_ctx_out)
    {
        avio_close(_fmt_ctx_out->pb);
    }
    
    avformat_close_input(&_fmt_ctx_spk);
    avformat_close_input(&_fmt_ctx_mic);
    avformat_free_context(_fmt_ctx_out);
    
}

int main(){
    int ret =0;
    initRecorder();
    
    char file_name[128];
    char *out_type=".mp3";
    time_t rawtime;
    int speaker_count=0;
    int micro_count=0;
    
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
    
    
    
    int tmp_fifo_failed=0;
    int64_t frame_count=0;
    while (_state!=CaptureState::FINISHED) {
        
        int ret =0;
        AVFrame *pFrame_spk=av_frame_alloc();
        AVFrame *pFrame_mic=av_frame_alloc();
        AVPacket packet_out;
        int got_packet_ptr;
        int fifo_spk_size=av_audio_fifo_size(_fifo_spk);
        int fifo_mic_size=av_audio_fifo_size(_fifo_mic);
        int frame_spk_min_size=_fmt_ctx_spk->streams[_index_spk]->codec->frame_size;
        int frame_mic_min_size=_fmt_ctx_mic->streams[_index_mic]->codec->frame_size;
        
        if(fifo_spk_size>=frame_spk_min_size||fifo_mic_size>=frame_mic_min_size){
            tmp_fifo_failed=0;
            if (fifo_spk_size>=frame_spk_min_size) {
                pFrame_spk->nb_samples=frame_spk_min_size;
                pFrame_spk->channel_layout=_fmt_ctx_spk->streams[_index_spk]->codec->channel_layout;
                pFrame_spk->format=_fmt_ctx_spk->streams[_index_spk]->codec->sample_fmt;
                pFrame_spk->sample_rate=_fmt_ctx_spk->streams[_index_spk]->codec->sample_rate;
                av_frame_get_buffer(pFrame_spk, 0);
                
                pthread_mutex_lock(&_speaker_mutex);
                ret=av_audio_fifo_read(_fifo_spk, (void**)pFrame_spk->data, frame_spk_min_size);
                pthread_mutex_unlock(&_speaker_mutex);
                
                pFrame_spk->pts=speaker_count*frame_spk_min_size;
                
                speaker_count++;
                ret=av_buffersrc_add_frame(_filter_ctx_src_spk, pFrame_spk);
                
                if (ret<0) {
                    printf("Mixer:failed to call av_buffersrc_add_frame(speaker)\n");
                    break;
                }
            }
            
            if (fifo_mic_size>=frame_mic_min_size) {
                pFrame_mic->nb_samples=frame_mic_min_size;
                pFrame_mic->channel_layout=_fmt_ctx_mic->streams[_index_mic]->codec->channel_layout;
                pFrame_mic->format=_fmt_ctx_mic->streams[_index_mic]->codec->sample_fmt;
                pFrame_mic->sample_rate=_fmt_ctx_mic->streams[_index_mic]->codec->sample_rate;
                av_frame_get_buffer(pFrame_mic, 0);
                
                pthread_mutex_lock(&_microphone_mutex);
                ret=av_audio_fifo_read(_fifo_mic, (void**)pFrame_mic->data, frame_mic_min_size);
                pthread_mutex_unlock(&_microphone_mutex);
                
                
                pFrame_mic->pts=micro_count*frame_mic_min_size;
                
                
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
                    printf("Mixer:failed to call av_buffersink_get_frame_flags\n");
                    break;
                }
                
                if (pFrame_out->data[0]!=NULL) {
                    av_init_packet(&packet_out);
                    packet_out.data=NULL;
                    packet_out.size=0;
                    ret=avcodec_encode_audio2(_fmt_ctx_out->streams[_index_a_out]->codec
                                              , &packet_out, pFrame_out, &got_packet_ptr);
                    if (ret<0) {
                        printf("Mixer:failed to call avcodec_encode_audio2\n");
                        break;
                    }
                    if (got_packet_ptr) {
                        packet_out.stream_index=_index_a_out;
                        packet_out.pts=frame_count*_fmt_ctx_out->streams[_index_a_out]->codec->frame_size;
                        packet_out.dts=packet_out.pts;
                        packet_out.duration=_fmt_ctx_out->streams[_index_a_out]->codec->frame_size;
                        
                        packet_out.pts = av_rescale_q_rnd(packet_out.pts,
                                                          _fmt_ctx_out->streams[_index_a_out]->codec->time_base,
                                                          _fmt_ctx_out->streams[_index_a_out]->time_base,
                                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        packet_out.dts = packet_out.pts;
                        packet_out.duration = av_rescale_q_rnd(packet_out.duration,
                                                               _fmt_ctx_out->streams[_index_a_out]->codec->time_base,
                                                               _fmt_ctx_out->streams[_index_a_out]->time_base,
                                                               (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                        frame_count++;
                        ret=av_interleaved_write_frame(_fmt_ctx_out, &packet_out);
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
    av_write_trailer(_fmt_ctx_out);
   
    
    return ret;
}






