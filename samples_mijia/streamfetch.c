/*
 * Copyright (c) 2017 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
 */
#include <sys/select.h>
#include <shbf/receiver.h>
#include <shbf/receiver-ev.h>

#include <sys/signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>
#include <sys/prctl.h>
#include <miio/log.h>
#include <unistd.h>

#include <miio/frameinfo.h>
#include "streamfetch.h"
//#include "streamsend.h"

//#include "miio/av_cmd.h"
#include "mediaformat.h"
//#include "clientinfo.h"

volatile int running = 1;
extern int gProcessRun;

#define AUDIO_STREAM_CHN 100

shbf_rcv_t shbf_audio_stream = NULL;
shbf_rcv_t shbf_video_mainstream = NULL;
shbf_rcv_t shbf_video_substream = NULL;
int sfd_audio = -1;
int sfd_main = -1;
int sfd_sub = -1;

int mainvideoconnected = 0;
int subvideoconnected = 0;
int audioconnected = 0;

int param_main = VIDEO_STREAM_CHN_1080P;
int param_sub = VIDEO_STREAM_CHN_360P;
int param_audio = AUDIO_STREAM_CHN;

ev_async async;
struct ev_loop *loop;

static shbfev_rcv_t main_stream_recv_ev;
static shbfev_rcv_t sub_stream_recv_ev;
static shbfev_rcv_t audio_stream_recv_ev;

shbfev_rcv_t audio_stream_send_ev;

time_t lastmainstremtime;
time_t lastsubstremtime;
time_t lastaudiostremtime;

unsigned int lastmainframesq = -1;
unsigned int lastsubframesq = -1;
unsigned int lastaudioframesq = -1;

unsigned int mainframelostcnt = 0;
unsigned int subframelostcnt = 0;
unsigned int audioframelostcnt = 0;

unsigned int appstarttime = 0;

void (*g_fnVideoFrameHandler) (int videochn, MIIO_IMPEncoderStream * streamInfo, char *data, unsigned int length);
void (*g_fnAudioFrameHandler) (MIIO_IMPAudioFrame * frameinfo, char *data, unsigned int length);

void send_video_stream (int videochn, MIIO_IMPEncoderStream * streamInfo, char *data, unsigned int length)
{
	if (0 == videochn) {
		return;
	}

	Frame frame;
	frame.frameData = data;
	frame.size = length;
	frame.presentationTs = streamInfo->timestamp;

	printf ("video frame size: %d\n", frame.size);
	send_frame(&frame, 1);
}

void send_audio_stream (MIIO_IMPAudioFrame * frameinfo, char *data, unsigned int length)
{
	Frame frame;
	frame.frameData = data;
	frame.size = length;
	frame.presentationTs = frameinfo->timeStamp;

	//send_frame(&frame, 0);
}


void stop_ev_loop(void)
{
	ev_break(loop, EVBREAK_ALL);
}

#if 0
static double time_diff(const struct timespec *start, const struct timespec *end)
{
	return end->tv_sec - start->tv_sec + (end->tv_nsec - start->tv_nsec) / (1.0 * 1000 * 1000 * 1000);
}
#endif

int on_recv_video_stream(void *user_data, void *buf)
{
	size_t size=0;
	int data_size = 0;
	static int waittingforIFrame_main = 0;
	static int waittingforIFrame_sub = 0;

	if (NULL == buf) {
		return -1;
	}

	size = shbf_get_size(buf);
	data_size = size - sizeof (MIIO_IMPEncoderStream) ;       
	if (data_size <= 0) {
		shbf_free(buf);
		return -1;
	}         


	int stream_idx = *(int *)user_data;
	MIIO_IMPEncoderStream* streamInfo =  (MIIO_IMPEncoderStream* )buf;
	char *data = (char *)buf + sizeof (MIIO_IMPEncoderStream);      

	if (0 == stream_idx) {
		if (waittingforIFrame_main > 0) {
				if (1 == streamInfo->key_frame) {
						log_debug ("main waiting for I frame fin, wait cnt: %d\n", waittingforIFrame_main);
						waittingforIFrame_main = 0;
					} else {
							// drop this frame
							log_debug ("main Waitting for next I frame: %d\n", waittingforIFrame_main);
							waittingforIFrame_main++;
							lastmainframesq = streamInfo->seq;
							shbf_free(buf);                                                                 
							return 0;
					}
		} else {
				if ((streamInfo->seq - lastmainframesq  != 1) && (lastmainframesq != -1)) {
							// if there is any frame dropped, just drop the followed all p Frame and waitting for Next I frame
							if (0 == waittingforIFrame_main) {
									waittingforIFrame_main = 1;
									//ForceKeyFrame (VIDEO_STREAM_CHN_1080P);
							}
							
							mainframelostcnt += (streamInfo->seq - lastmainframesq - 1);
							log_info ("main stream frame lost: cur frame no: %d last frame no: %d, main/sub/audio lost cnt: [%d/%d/%d], app run time:[%d]\n", 
								streamInfo->seq, lastmainframesq, mainframelostcnt, subframelostcnt, audioframelostcnt,  (int)time(NULL) - appstarttime);                                        
							lastmainframesq = streamInfo->seq;
							shbf_free(buf);              
							return -1;
				}
		}
		
		lastmainframesq = streamInfo->seq;
		lastmainstremtime = time (NULL);
	} else if (1== stream_idx) {
		if (waittingforIFrame_sub > 0) {
		if (1 == streamInfo->key_frame) {
				log_debug ("sub waiting for I frame fin, wait cnt: %d\n", waittingforIFrame_sub);
				waittingforIFrame_sub = 0;
			} else {
					// drop this frame
					log_debug ("sub Waitting for next I frame: %d\n", waittingforIFrame_sub);                                             
					waittingforIFrame_sub++;
					lastsubframesq = streamInfo->seq;                                             
					shbf_free(buf);                  
					return 0;
			}
		} else {                
			if ((streamInfo->seq - lastsubframesq  != 1) && (lastsubframesq != -1)) {
						if (0 == waittingforIFrame_sub)  {
								waittingforIFrame_sub = 1;
								//ForceKeyFrame(VIDEO_STREAM_CHN_360P);                                                
						}
						subframelostcnt += (streamInfo->seq - lastsubframesq - 1);                                        
						log_info ("sub stream frame lost: cur fream no: %d last frame no: %d, main/sub/audio lost cnt: [%d/%d/%d], app run time: [%d]\n", 
							streamInfo->seq, lastsubframesq, mainframelostcnt, subframelostcnt, audioframelostcnt, (int)time (NULL) - appstarttime);                                       
						lastsubframesq = streamInfo->seq;
						shbf_free(buf);  
						return -1;
			}
		}

			lastsubframesq = streamInfo->seq;
			lastsubstremtime = time (NULL);
	}
	g_fnVideoFrameHandler(stream_idx, streamInfo, data, data_size);                            

	shbf_free(buf);                                                   
	return 0;
}


int on_recv_audio_stream(void *user_data, void *buf)
{
    size_t  size = 0;
	int data_size = 0;

	if (NULL == buf) {
		log_err("on_recv_audio_stream err audio buf is NULL\n");
		return -1;
	}

	size = shbf_get_size(buf);
	data_size = size - sizeof(MIIO_IMPAudioFrame);

	if (data_size <= 0) {
		shbf_free(buf);
		return -1;
	}


	MIIO_IMPAudioFrame *streamInfo = (MIIO_IMPAudioFrame *) buf;
	char *data = (char *)buf + sizeof(MIIO_IMPAudioFrame);

	if (streamInfo->seq > 0) {
		if (streamInfo->seq - lastaudioframesq != 1) {
			if (lastaudioframesq != -1) {
				audioframelostcnt += (streamInfo->seq - lastaudioframesq - 1);
				log_info
					("audio frame lost: cur fream no: %d last frame no: %d, main/sub/audio lost cnt: [%d/%d/%d], app run time:[%d]\n",
						streamInfo->seq, lastaudioframesq, mainframelostcnt,
						subframelostcnt, audioframelostcnt, (int)time(NULL) - appstarttime);
			}
		}

		lastaudioframesq = streamInfo->seq;
	}

	lastaudiostremtime = time(NULL);
	g_fnAudioFrameHandler(streamInfo, data, data_size);

	//        printf("Received buffer: tick=%lu size=%lu message=%s\n", tick, size,  buf);
	shbf_free(buf);
	return 0;
	
}

static void on_close_stream(void *user_data)
{

	int stream_idx = -1;
	if (NULL != user_data) {
		stream_idx = *(int *)user_data;
		if (VIDEO_STREAM_CHN_1080P == stream_idx) {
			sfd_main = -1;
//                    mainvideoconnected = 0;
		} else if (VIDEO_STREAM_CHN_360P == stream_idx) {
			sfd_sub = -1;
//                    subvideoconnected = 0;
		} else if (AUDIO_STREAM_CHN == stream_idx) {
			sfd_audio = -1;
//                    audioconnected =0;
		}
	}
	log_info("%d receiver closed!time: %d\n", stream_idx, (int)time(NULL));
	running = 0;
}

static void on_stream_start(void *user_data)
{

	int stream_idx = -1;
	if (NULL != user_data) {
		stream_idx = *(int *)user_data;

		if (VIDEO_STREAM_CHN_1080P == stream_idx) {
			mainvideoconnected++;
		} else if (VIDEO_STREAM_CHN_360P == stream_idx) {
			subvideoconnected++;
		} else if (AUDIO_STREAM_CHN == stream_idx) {
			audioconnected++;
		}

		log_info("stream start: %d,  main/sub/audio stream reconn times:[ %d/%d/%d],  time: %d\n", stream_idx,
			 mainvideoconnected, subvideoconnected, audioconnected, (int)time(NULL));
	}

	running = 0;
}


static void loop_check_cb(struct ev_loop *loop, ev_timer * w, int revents)
{

	time_t now = time(NULL);
	if (now - lastmainstremtime > 2) {
		log_info("can not get main stream, time:%d\n", (int)time(NULL));
	}

	if (now - lastsubstremtime > 2) {
		log_info("can not get sub stream, time:%d\n", (int)time(NULL));
	}

	if (now - lastaudiostremtime > 2) {
		log_info("can not get audio stream, time:%d\n", (int)time(NULL));
	}
	//       printf ("now: %lu,lastmainstremtime: %lu lastaudiostremtime:%d\n", now, lastmainstremtime, lastaudiostremtime);
}

int start_fetchstream()
{
	appstarttime = time(NULL);

	int ret = prctl(PR_SET_NAME, "av_fetchstream\0", NULL, NULL, NULL);
	if (ret != 0) {
		log_warning("prctl setname failed\n");
	}

	shbf_rcv_global_init();

	loop = EV_DEFAULT;
	ev_timer timer_loop_check;
	ev_timer_init(&timer_loop_check, loop_check_cb, 0, 5);
	ev_timer_again(loop, &timer_loop_check);
  

	int param_main = VIDEO_STREAM_CHN_1080P;
	int param_sub = VIDEO_STREAM_CHN_360P;
	int param_audio = 3;
	int param_audio_send = 4;
	g_fnVideoFrameHandler = send_video_stream;
	g_fnAudioFrameHandler = send_audio_stream;

	main_stream_recv_ev = shbfev_rcv_create(loop, VIDEO_MAINSTREAM_CONTROLLER);
	shbfev_rcv_event(main_stream_recv_ev, SHBF_RCVEV_SHARED_BUFFER, SHBF_EVT_HANDLE(on_recv_video_stream),
			 &param_main);
	shbfev_rcv_event(main_stream_recv_ev, SHBF_RCVEV_CLOSE, SHBF_EVT_HANDLE(on_close_stream), &param_main);
	shbfev_rcv_event(main_stream_recv_ev, SHBF_RCVEV_READY, SHBF_EVT_HANDLE(on_stream_start), &param_main);
	shbfev_rcv_start(main_stream_recv_ev);

	sub_stream_recv_ev = shbfev_rcv_create(loop, VIDEO_SUBSTREAM_CONTROLLER);
	shbfev_rcv_event(sub_stream_recv_ev, SHBF_RCVEV_SHARED_BUFFER, SHBF_EVT_HANDLE(on_recv_video_stream),
			 &param_sub);
	shbfev_rcv_event(sub_stream_recv_ev, SHBF_RCVEV_CLOSE, SHBF_EVT_HANDLE(on_close_stream), &param_sub);
	shbfev_rcv_event(sub_stream_recv_ev, SHBF_RCVEV_READY, SHBF_EVT_HANDLE(on_stream_start), &param_sub);

	shbfev_rcv_start(sub_stream_recv_ev);

	audio_stream_recv_ev = shbfev_rcv_create(loop, AI_CONTROLLER);
	shbfev_rcv_event(audio_stream_recv_ev, SHBF_RCVEV_SHARED_BUFFER, SHBF_EVT_HANDLE(on_recv_audio_stream),
			 &param_audio);
	shbfev_rcv_event(audio_stream_recv_ev, SHBF_RCVEV_CLOSE, SHBF_EVT_HANDLE(on_close_stream), &param_audio);
	shbfev_rcv_event(audio_stream_recv_ev, SHBF_RCVEV_READY, SHBF_EVT_HANDLE(on_stream_start), &param_audio);

	shbfev_rcv_start(audio_stream_recv_ev);

	audio_stream_send_ev = shbfev_rcv_create(loop, AO_CONTROLLER);
//      shbfev_rcv_event(audio_stream_send_ev, SHBF_RCVEV_SHARED_BUFFER, SHBF_EVT_HANDLE(on_recv_video_stream), &param_main);
	shbfev_rcv_event(audio_stream_send_ev, SHBF_RCVEV_CLOSE, SHBF_EVT_HANDLE(on_close_stream), &param_audio_send);
	shbfev_rcv_event(audio_stream_send_ev, SHBF_RCVEV_READY, SHBF_EVT_HANDLE(on_stream_start), &param_audio_send);
	shbfev_rcv_start(audio_stream_send_ev);

	//ev_run(EV_DEFAULT, 0);

	ev_async_init(&async, stop_ev_loop);
	ev_async_start(loop, &async);
	ev_run(loop, 0);
	ev_timer_again(loop, &timer_loop_check);

	shbfev_rcv_destroy(main_stream_recv_ev);
	shbfev_rcv_destroy(sub_stream_recv_ev);
	shbfev_rcv_destroy(audio_stream_recv_ev);
	shbfev_rcv_destroy(audio_stream_send_ev);

	shbf_rcv_global_exit();
	ev_default_destroy();

//        log_err ("thread_fetchstream exit \n");
	pthread_exit((void *)-5);
	log_err("thread_fetchstream exit, whole progress exit\n");
	exit(0);
	return NULL;
}

