/*
 * Copyright (c) 2017 Beijing Xiaomi Mobile Software Co., Ltd. All rights reserved.
 * mp4streamfetch.h
 * this file will demux the mp4 to video and audio stream
 * add by yangyouqing @20171107
 */

#ifndef _MP4_STREAM_FETCH_H
#define _MP4_STREAM_FETCH_H

#include <stdio.h>
#include <stdint.h>
#include <mp4v2/mp4v2.h>

#define MAX_VIDEO_FRAME_LEN (255 * 1024)
#define MAX_AUDIO_FRAME_LEN (8 * 1024)
#define MAX_VIDEO_SPS_PPS_LEN (4096 + 4096)	// Get from ffmpeg, max sps len:4096  max pps len: 4096
//#define MAX_VIDEO_SPS_PPS_LEN 1024    

typedef struct _mediaInfo {
	//      int audioChnCnt;   // 0: no audio, 1: have 1 audio,  >1: just handle the first channel audio
	//      int videoChn;          // 0: no video, 1: have 1 video,  >1: just handle the first channel video        
	int32_t audioType;	// todo
	int32_t videoType;
//        int audioCodec;
//        int videoCodec;
    int audioFrameCnt;
    int videoFrameCnt;
    uint32_t videotrackId;
    uint32_t audiotrackId;
    uint32_t videotimescale;
    uint32_t audiotimescale;
    uint32_t duration;
	int width;
	int height;
	uint32_t spsppslen;
	char spsppsbuf[MAX_VIDEO_SPS_PPS_LEN];
	uint64_t startts;	//  start timestamp of mp4 file        
	uint32_t fps;
} TMediaInfo;

// each video frame info 
typedef struct _videoframeinfo {
	TMediaInfo *ptMediaInfo;
    int iskey;
	int frametype;		
	uint64_t timestamp;	//  unit in macro second
	uint64_t pts;
	int seq;
	char *buf;
	uint32_t buflen;
	char *tsbuf;
	int tslen;
} TVideoFrameInfo;

// each audio frame info
typedef struct _audioframeinfo {
//      uint64_t startts;       //  start timestamp of mp4 file
	uint64_t timestamp;	// unit in macro second
	uint64_t pts;
	char buf[MAX_AUDIO_FRAME_LEN];
	uint32_t buflen;
	char *tsbuf;
	int tslen;
	TMediaInfo *ptMediaInfo;
} TAudioFrameInfo;


FILE * openFiles(char *strFilepath);

uint64_t GetMp4StartTimeFromFileName(char *filename);

int GetMp4Info(FILE * mp4File, TMediaInfo * ptMediaInfo);
void showmediainfo(TMediaInfo * ptMediaInfo);

int SeektoKeyFrame(FILE * mp4File, int nTrackId, TVideoFrameInfo * pFrame);


int GetVideoFrameInfoFrommp4(MP4FileHandle hFile, MP4TrackId trackId, MP4SampleId videosampleId,
			      TVideoFrameInfo * pFrame);

int GetAudioFrameDataFromMp4(MP4FileHandle hFile, MP4TrackId trackId, MP4SampleId sampleId, 
                                    TAudioFrameInfo * pFrame);

#endif // _MP4_STREAM_FETCH_H
