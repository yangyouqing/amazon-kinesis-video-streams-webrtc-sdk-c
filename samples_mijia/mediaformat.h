/*
 * Copyright (c) 2017 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
* mediaformat.h
* this file use to  define platform related 
 * add by yangyouqing @20171123
 */
#ifndef _MEDIA_FORMAT_H
#define _MEDIA_FORMAT_H

//#define AUDIO_CODEC_AAC   0x88
//#define AUDIO_CODEC_G711A 0x01
#define AUDIO_FORMAT_AAC	// modify this define for test which audio format
#define AUDIO_FRAME_SIZE 700
#define AUDIO_ENCODED_SIZE 40
#define USE_AUDIO_CODEC_G711

#ifdef USE_AUDIO_CODEC_G711
#define AUDIO_CODEC MISS_CODEC_AUDIO_G711A
#else
#define AUDIO_CODEC MISS_CODEC_AUDIO_AAC
#endif

#ifndef AUDIO_FPS
#define AUDIO_FPS 25
#endif

#ifndef VIDEO_FPS
#define VIDEO_FPS 20
#endif

#ifndef VIDEO_GOP
#define VIDEO_GOP 60		// 20 * 3
#endif

#define TOTAL_AV_FPS (VIDEO_FPS + AUDIO_FPS)
#define ALL_FRAMES_IN_GOP  ((VIDEO_FPS + AUDIO_FPS) * (VIDEO_GOP / VIDEO_FPS))

#if HAS_720P
#define VIDEO_STREAM_CHN_1080P 0
#define VIDEO_STREAM_CHN_720P 1
#define VIDEO_STREAM_CHN_360P 2
#else
#define VIDEO_STREAM_CHN_1080P 0
#define VIDEO_STREAM_CHN_360P 1
#endif

#define VIDEO_FRAME_1080P_W  1920
#define VIDEO_FRAME_1080P_H   1080
#define VIDEO_FRAME_360P_W   640
#define VIDEO_FRAME_360P_H   360

#define AUDIO_BUF_SIZE  (1024 * 10)	//speaker recv buff

#endif //_MEDIA_FORMAT_H
