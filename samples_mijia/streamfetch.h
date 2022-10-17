/*
 * Copyright (c) 2017 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
* streamfetch.h
 * add by yangyouqing @20171107
 */

#ifndef _STREAM_FETCH_H
#define _STREAM_FETCH_H

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>

#define FETCH_AV_SHBF_NUM				6

#define VIDEO_MAINSTREAM_CONTROLLER 	"/run/video_mainstream"
#define VIDEO_SUBSTREAM_CONTROLLER	"/run/video_substream"
#define VIDEO_RAWSTREAM_CONTROLLER	"/run/video_rawstream"
#define JPEG_CONTROLLER			"/run/jpeg_snap"
#define AI_CONTROLLER 			"/run/audio_in"
#define AO_CONTROLLER 			"/run/audio_play"
#define MD_CONTROLLER			"/run/md_event"


#define log_debug printf
#define log_info printf
#define log_err printf
#define log_warning printf

int start_fetchstream();
STATUS send_frame(PFrame pFrame, UINT32 isVideo);

#endif //_STREAM_FETCH_H