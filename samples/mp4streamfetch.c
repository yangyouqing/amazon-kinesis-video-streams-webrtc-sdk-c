#include <libgen.h>
#include <string.h>
#include <stdlib.h>
#include <mp4v2/mp4v2.h>
#include "mp4streamfetch.h"


/* CODEC ID */
typedef enum 
{
	MEDIA_CODEC_UNKNOWN			= 0x00,
	MEDIA_CODEC_VIDEO_MPEG4		= 0x4C,
	MEDIA_CODEC_VIDEO_H263		= 0x4D,
	MEDIA_CODEC_VIDEO_H264		= 0x4E,
	MEDIA_CODEC_VIDEO_MJPEG		= 0x4F,
	MEDIA_CODEC_VIDEO_HEVC      = 0x50,

    MEDIA_CODEC_AUDIO_AAC       = 0x88,   // 2014-07-02 add AAC audio codec definition
    MEDIA_CODEC_AUDIO_G711U     = 0x89,   //g711 u-law
    MEDIA_CODEC_AUDIO_G711A     = 0x8A,   //g711 a-law	
    MEDIA_CODEC_AUDIO_ADPCM     = 0X8B,
	MEDIA_CODEC_AUDIO_PCM		= 0x8C,
	MEDIA_CODEC_AUDIO_SPEEX		= 0x8D,
	MEDIA_CODEC_AUDIO_MP3		= 0x8E,
    MEDIA_CODEC_AUDIO_G726      = 0x8F,

}ENUM_CODECID;

static const char nalHeader[] = { 0x00, 0x00, 0x00, 0x01 };

#define log_trace(fmt, args...) printf( fmt, ##args)
#define log_debug(fmt, args...) printf( fmt, ##args)
#define log_info(fmt, args...) printf( fmt, ##args)
#define log_warning(fmt, args...) printf( fmt, ##args)
#define log_error(fmt, args...) printf( fmt, ##args)
#define log_err(fmt, args...) printf( fmt, ##args)
/************* This code is copy from ffmpeg adtsenc.c**********/

typedef struct PutBitContext {
	uint32_t bit_buf;
	int bit_left;
	uint8_t *buf, *buf_ptr, *buf_end;
	int size_in_bits;
} PutBitContext;

#ifndef AV_WB32
#define AV_WB32(p, darg) do {                \
        unsigned d = (darg);                    \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

/**
 * Initialize the PutBitContext s.
 *
 * @param buffer the buffer where to put bits
 * @param buffer_size the size in bytes of buffer
 */
static inline void init_put_bits(PutBitContext * s, uint8_t * buffer, int buffer_size)
{
	if (buffer_size < 0) {
		buffer_size = 0;
		buffer = NULL;
	}

	s->size_in_bits = 8 * buffer_size;
	s->buf = buffer;
	s->buf_end = s->buf + buffer_size;
	s->buf_ptr = s->buf;
	s->bit_left = 32;
	s->bit_buf = 0;
}

/**
 * Write up to 31 bits into a bitstream.
 * Use put_bits32 to write 32 bits.
 */
static inline void put_bits(PutBitContext * s, int n, unsigned int value)
{
	unsigned int bit_buf;
	int bit_left;

	//  av_assert2(n <= 31 && value < (1U << n));

	bit_buf = s->bit_buf;
	bit_left = s->bit_left;

	/* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
	bit_buf |= value << (32 - bit_left);
	if (n >= bit_left) {
		if (3 < s->buf_end - s->buf_ptr) {
			AV_WL32(s->buf_ptr, bit_buf);
			s->buf_ptr += 4;
		} else {
			//   av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
			//   av_assert2(0);
		}
		bit_buf = value >> bit_left;
		bit_left += 32;
	}
	bit_left -= n;
#else
	if (n < bit_left) {
		bit_buf = (bit_buf << n) | value;
		bit_left -= n;
	} else {
		bit_buf <<= bit_left;
		bit_buf |= value >> (n - bit_left);
		if (3 < s->buf_end - s->buf_ptr) {
			AV_WB32(s->buf_ptr, bit_buf);
			s->buf_ptr += 4;
		} else {
			//  av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
			//  av_assert2(0);
		}
		bit_left += 32 - n;
		bit_buf = value;
	}
#endif

	s->bit_buf = bit_buf;
	s->bit_left = bit_left;
}

/**
 * Pad the end of the output stream with zeros.
 */
static inline void flush_put_bits(PutBitContext * s)
{
#ifndef BITSTREAM_WRITER_LE
	if (s->bit_left < 32)
		s->bit_buf <<= s->bit_left;
#endif
	while (s->bit_left < 32) {
		//  av_assert0(s->buf_ptr < s->buf_end);
#ifdef BITSTREAM_WRITER_LE
		*s->buf_ptr++ = s->bit_buf;
		s->bit_buf >>= 8;
#else
		*s->buf_ptr++ = s->bit_buf >> 24;
		s->bit_buf <<= 8;
#endif
		s->bit_left += 8;
	}
	s->bit_left = 32;
	s->bit_buf = 0;
}

#define ADTS_MAX_FRAME_BYTES ((1 << 13) - 1)
#define ADTS_HEADER_SIZE 7

#define ADTS_HEADER_SAMPLE_RATE_INDEX_44100 0x4	// 44100HZ
#define ADTS_HEADER_SAMPLE_RATE_INDEX_16000 0x8	// 16000HZ

#define ADTS_HEADER_CHANNEL_SINGLE 1	// 1 CHANNEL
#define ADTS_HEADER_CHANNEL_STEREO 2	// 1 CHANNEL


static int adts_write_frame_header(uint8_t * buf, int size, int sample_rate_idx, int channel_size)
{
	PutBitContext pb;

	unsigned int full_frame_size = (unsigned int)ADTS_HEADER_SIZE + size + 0;
	if (full_frame_size > ADTS_MAX_FRAME_BYTES) {
		log_error("ADTS frame size too large: %u (max %d)\n", full_frame_size, ADTS_MAX_FRAME_BYTES);
		return 0;
	}

	init_put_bits(&pb, buf, ADTS_HEADER_SIZE);

	/* adts_fixed_header */
	put_bits(&pb, 12, 0xfff);	/* syncword */
	put_bits(&pb, 1, 1);	/* ID */

	put_bits(&pb, 2, 0);	/* layer */
	put_bits(&pb, 1, 1);	/* protection_absent */
	put_bits(&pb, 2, 1);	/* profile_objecttype */// Main profile
	put_bits(&pb, 4, sample_rate_idx);
	put_bits(&pb, 1, 0);	/* private_bit */
	put_bits(&pb, 3, channel_size);	/* channel_configuration */
	put_bits(&pb, 1, 0);	/* original_copy */
	put_bits(&pb, 1, 0);	/* home */

	/* adts_variable_header */
	put_bits(&pb, 1, 0);	/* copyright_identification_bit */
	put_bits(&pb, 1, 0);	/* copyright_identification_start */
	put_bits(&pb, 13, full_frame_size);	/* aac_frame_length */
	put_bits(&pb, 11, 0x7ff);	/* adts_buffer_fullness */
	put_bits(&pb, 2, 0);	/* number_of_raw_data_blocks_in_frame */

	flush_put_bits(&pb);

	return 0;
}

/**********************split line bottom*****************************************/

FILE * openFiles(char *strFilepath)
{
    FILE *mp4File = (FILE *)MP4Read(strFilepath);
	
	if (mp4File == NULL) {
		log_info("mp4 file read error");
	}

	return mp4File;
}



int getmp4sps_pps(MP4FileHandle hFile, MP4TrackId trackId, unsigned char *strSpsPpsBuf, int *pSpsppsByteCnt)
{
	const char nalHeader[] = { 0x00, 0x00, 0x00, 0x01 };
	int spsppsByteCnt = 0;
	// read sps/pps
	uint8_t **seqheader;
	uint8_t **pictheader;
	uint32_t *pictheadersize;
	uint32_t *seqheadersize;
	uint32_t ix;
	bool bRet = 0;

	bRet = MP4GetTrackH264SeqPictHeaders(hFile, trackId, &seqheader, &seqheadersize, &pictheader, &pictheadersize);
	if (true != bRet) {
		return -1;
	}
	// copy 0 0 0 1 to front of sps
	memcpy((unsigned char *)strSpsPpsBuf + spsppsByteCnt, nalHeader, 4);
	spsppsByteCnt += 4;

	for (ix = 0; seqheadersize[ix] != 0; ix++) {
		if (spsppsByteCnt + seqheadersize[ix] > MAX_VIDEO_SPS_PPS_LEN) {
			log_warning("Sps len is invalid: %d\n", seqheadersize[ix]);
			continue;
		}

		memcpy((unsigned char *)strSpsPpsBuf + spsppsByteCnt, seqheader[ix], seqheadersize[ix]);
		spsppsByteCnt += seqheadersize[ix];
		free(seqheader[ix]);
	}
	free(seqheader);
	free(seqheadersize);

	// copy 0 0 0 1 to front of pps
	memcpy((unsigned char *)strSpsPpsBuf + spsppsByteCnt, nalHeader, 4);
	spsppsByteCnt += 4;

	for (ix = 0; pictheadersize[ix] != 0; ix++) {
		if (spsppsByteCnt + pictheadersize[ix] > MAX_VIDEO_SPS_PPS_LEN) {
			log_warning("Pps len is invalid: %d\n", pictheadersize[ix]);
			continue;
		}
		memcpy((unsigned char *)strSpsPpsBuf + spsppsByteCnt, pictheader[ix], pictheadersize[ix]);
		spsppsByteCnt += pictheadersize[ix];
		free(pictheader[ix]);
	}
	free(pictheader);
	free(pictheadersize);
	*pSpsppsByteCnt = spsppsByteCnt;
	return 0;
}


int GetH264VideoFrameInfoFrommp4(MP4FileHandle hFile, MP4TrackId trackId, MP4SampleId videosampleId,
			      TVideoFrameInfo * pFrame)
{
    uint8_t *pVideoSample = NULL;
	uint32_t videosampleSize = 0;
	uint64_t vtimestamp = 0;
	int nSendBufLen = 0;
	char *videobuf = pFrame->buf;
	if (!MP4ReadSample
	    (hFile, trackId, videosampleId, &pVideoSample, &videosampleSize, &vtimestamp, NULL, NULL, NULL)) {
		log_err("read sampleId %u error\n", videosampleId);
		if (pVideoSample != NULL) {
			free(pVideoSample);
			pVideoSample = NULL;
		}
		return -1;
	}

	if (videosampleSize > MAX_VIDEO_FRAME_LEN) {
		log_err ( "error video sample size:%d\n", videosampleSize);
		return 0;
	}
//	printf("VideoSampleId: %d, SampleSize: %d timestamp: %llu \n", videosampleId, videosampleSize, vtimestamp);

    pFrame->pts = vtimestamp;
    int vtype = (*((unsigned char *)pVideoSample + 4) & 0x1F);
//    printf ("video frame type: %d\n", vtype);
    pFrame->frametype = vtype;
	pFrame->iskey = 0;
	if (vtype == 1)	{// whether it is P frame
		if (NULL != videobuf) {
            #if 1
			memcpy(videobuf, nalHeader, 4);
			memcpy(videobuf + 4, pVideoSample + 4, videosampleSize - 4);
			nSendBufLen = videosampleSize;
            #else 
			memcpy(videobuf, pVideoSample, videosampleSize);
			nSendBufLen = videosampleSize;
            #endif
		}
		
	} else if (vtype == 5) { // Just I frame
		pFrame->iskey = 1;
	    if ((NULL != videobuf) && (NULL != pFrame->ptMediaInfo)) {
			memcpy(videobuf, pFrame->ptMediaInfo->spsppsbuf, pFrame->ptMediaInfo->spsppslen);
			memcpy(videobuf + pFrame->ptMediaInfo->spsppslen, nalHeader, 4);
			memcpy(videobuf + pFrame->ptMediaInfo->spsppslen + 4, pVideoSample + 4, videosampleSize - 4);
			nSendBufLen = pFrame->ptMediaInfo->spsppslen + videosampleSize;
		}
	} else if (vtype == 7){ // sps/pps/I frame
	/*	  	
	    00 00 00 16 67 4D 40 28 9D A8 1E 00 89 F9 61 00
	    00 03 00 01 00 00 03 00 28 84 00 00 00 04 68 EE
	    3C 80 00 01 98 81 65 B8 00 83 7F 19 12 22 0E E6
	    ...............................................
	*/
		pFrame->iskey = 1;
		// just skip sps-pps, then got i frame
		if ((NULL != videobuf) && (NULL != pFrame->ptMediaInfo)) {
            // cpy sps pps from media info 
			memcpy(videobuf, pFrame->ptMediaInfo->spsppsbuf, pFrame->ptMediaInfo->spsppslen);
            // cpy nalu header
			memcpy(videobuf + pFrame->ptMediaInfo->spsppslen, nalHeader, 4);
            // skip sps pps 
            int ioffset = pFrame->ptMediaInfo->spsppslen + 4;
            int ilen = videosampleSize - ioffset;
			memcpy(videobuf + ioffset, pVideoSample + ioffset, ilen);
			nSendBufLen = ilen;
		}
	} else if (vtype == 6) {
	    printf ("SEI frame, ignore\n");
	} else {
        printf ("get wrong video frame type: %d\n", vtype);
        return -1;
	}

	/* the sample starttime is the timestamp of this frame which is presented in timescale unit,
	 * just transfer it to macro second.
	 */

	if (NULL != pFrame->ptMediaInfo) {
		pFrame->timestamp =
		    pFrame->ptMediaInfo->startts + (vtimestamp / (double)pFrame->ptMediaInfo->videotimescale) * 1000;
	}

	pFrame->buflen = nSendBufLen;

	if (pVideoSample != NULL) {
		free(pVideoSample);
		pVideoSample = NULL;
	}

    return pFrame->buflen;
} 

int GetVideoFrameInfoFrommp4(MP4FileHandle hFile, MP4TrackId trackId, MP4SampleId videosampleId,
			      TVideoFrameInfo * pFrame)
{
         if (MEDIA_CODEC_VIDEO_H264 == pFrame->ptMediaInfo->videoType) {
                    return   GetH264VideoFrameInfoFrommp4(hFile, trackId, videosampleId, pFrame);   //TODO	
          } else if (MEDIA_CODEC_VIDEO_HEVC == pFrame->ptMediaInfo->videoType) {
                    //return   GetH265VideoFrameInfoFrommp4(hFile, trackId, videosampleId, pFrame);   //TODO	
                    return   GetH264VideoFrameInfoFrommp4(hFile, trackId, videosampleId, pFrame); 
          } else {
               // log_debug ("%s, error codec type: %d\n", pFrame->ptMediaInfo->videoType);
                return -1;
          }
}

int GetAudioFrameDataFromMp4(MP4FileHandle hFile, MP4TrackId trackId, MP4SampleId sampleId, TAudioFrameInfo * pFrame)
{
	uint8_t *pAudioSample = NULL;
	uint32_t audiosampleSize = 0;
	uint64_t atimestamp = 0;
	char *audiobuf = pFrame->buf;
#define ADTS_HEADER_LEN 7
	unsigned char aacheader[] = { 0xFF, 0xF9, 0x60, 0x40, 0x20, 0x1F, 0xFC };	//adts header, more information on internet

	// read audio samples
	if (!MP4ReadSample(hFile, trackId, sampleId, &pAudioSample, &audiosampleSize, &atimestamp, NULL, NULL, NULL)) {
		log_info("read audio sampleId %u error\n", sampleId);
		if (pAudioSample != NULL) {
			free(pAudioSample);
			pAudioSample = NULL;
		}
	}
//      printf ("AudioSampleId: %d, AudioSampleSize: %d atimestamp: %llu \n", sampleId, audiosampleSize, atimestamp);

	if (audiosampleSize > MAX_AUDIO_FRAME_LEN) {
		log_err ( "error audio sample size:%d\n", audiosampleSize);
		return -1;
	}

	if (MEDIA_CODEC_AUDIO_AAC == pFrame->ptMediaInfo->audioType) {
		adts_write_frame_header((uint8_t *) aacheader, audiosampleSize, ADTS_HEADER_SAMPLE_RATE_INDEX_44100, ADTS_HEADER_CHANNEL_STEREO);
		memcpy(audiobuf, aacheader, ADTS_HEADER_LEN);
		memcpy(audiobuf + ADTS_HEADER_LEN, pAudioSample, audiosampleSize);
		pFrame->buflen = audiosampleSize + ADTS_HEADER_LEN;
		//memcpy(audiobuf, pAudioSample, audiosampleSize);
		//pFrame->buflen = audiosampleSize;
	} else if (MEDIA_CODEC_AUDIO_G711A == pFrame->ptMediaInfo->audioType) {
		memcpy(audiobuf, pAudioSample, audiosampleSize);
		pFrame->buflen = audiosampleSize;
	}

	if (pAudioSample != NULL) {
		free(pAudioSample);
		pAudioSample = NULL;
	}
	//  printf ("%02X %02X %02X %02X %02X %02X %02X samplesize: %u \n",aacheader[0], aacheader[1], aacheader[2],  aacheader[3], aacheader[4], aacheader[5], aacheader[6], audiosampleSize);

    pFrame->pts = atimestamp;
	pFrame->timestamp =
	    pFrame->ptMediaInfo->startts + (atimestamp / (double)pFrame->ptMediaInfo->audiotimescale) * 1000;
           return 0;
}

// if fail, return -1, or return the sampleid of the key frame
int SeektoKeyFrame(FILE * mp4File, int nTrackId, TVideoFrameInfo * pFrame)
{
	int nSampleId = 1;
//	TVideoFrameInfo tVideoFrame = { 0 };



	while (true) {
		GetVideoFrameInfoFrommp4(mp4File, nTrackId, nSampleId, pFrame);
		if (7 == pFrame->frametype || 5 == pFrame->frametype) {
			break;
		}

		nSampleId++;
	}
    printf ("SeektoKeyFrame, skip %d frames\n", nSampleId);

	return nSampleId;
}

/* 
    Judge whether have normal p frame. which is diffent with key P Frame.
    The old version have 2 kind of frames, 1: key frame   2: key p frame
    The new version have 3 kind of frames, 1: key frame   2: key p frame  3: normal p frame
           which can distinguished by the 5th byte of h264 data.  
           0x67 is key frame  0x41 is p frame and 0x01 is normal p frame                                       

*/


uint64_t GetMp4StartTimeFromFileName(char *path)
{
#if 0
	const char *filename = (const char *)basename(path);
	if (NULL == filename) {
		log_err("basename failed: [%s]\n", filename);
		return -1;
		//      goto LBL_READ_MP4_ERROR;
	}

	uint64_t startts = ParseTimestampFromFile(filename);
	if (0 == startts) {
		log_err("the file is incorrect:[%s]\n", filename);
		return -1;
	}

	return startts * 1000;
    #endif
    return 0;
}

void showmediainfo(TMediaInfo * ptMediaInfo)
{
	log_info("***************************mediainfo************************\n");
	log_info("audioFrameCnt: %d,  videoFrameCnt: %d\n", ptMediaInfo->audioFrameCnt, ptMediaInfo->videoFrameCnt);

	log_info("audiotrackId: %d, videotrackId: %d\n", ptMediaInfo->audiotrackId, ptMediaInfo->videotrackId);

	log_info("audiotimescale: %d, videotimescale: %d\n", ptMediaInfo->audiotimescale, ptMediaInfo->videotimescale);

	log_info("width: %d, height:%d\n", ptMediaInfo->width, ptMediaInfo->height);

	log_info("startts: %llu\n", ptMediaInfo->startts);
    log_info ("duration: %d\n", ptMediaInfo->duration);        
    log_info ("audio type: %d\n", ptMediaInfo->audioType);                
    log_info ("fps: %d\n", ptMediaInfo->fps);   
    log_info ("spsppslen: %d\n", ptMediaInfo->spsppslen);
}

int GetMp4Info(FILE * mp4File, TMediaInfo * ptMediaInfo)
{
	uint16_t videoWidth = 0;
	uint16_t videoHeight = 0;
	int duration = 0;
	int fps = 0;

	int ret = 0;

        uint32_t videotrackId = MP4_INVALID_TRACK_ID;
        uint32_t audiotrackId = MP4_INVALID_TRACK_ID;
        uint32_t numOfTracks = MP4GetNumberOfTracks(mp4File, NULL, 0);
        
  //      log_info( "numOfTracks: %d\n", numOfTracks);

	uint32_t tmpTrackId;
	// find video track
	for (tmpTrackId = 1; tmpTrackId <= numOfTracks; tmpTrackId++) {
		const char *trackType = MP4GetTrackType(mp4File, tmpTrackId);
		if (MP4_IS_VIDEO_TRACK_TYPE(trackType)) {
			videotrackId = tmpTrackId;

			videoHeight = MP4GetTrackVideoHeight(mp4File, videotrackId);
			videoWidth = MP4GetTrackVideoWidth(mp4File, videotrackId);

			//log_info( "MP4_IS_VIDEO_TRACK_TYPE:%d video height: %d video width: %d\n", videotrackId, videoHeight, videoWidth);
			//break;
		} else if (MP4_IS_AUDIO_TRACK_TYPE(trackType)) {
			audiotrackId = tmpTrackId;
		}
	}
	if (videotrackId == MP4_INVALID_TRACK_ID) {
		log_err("Can't find video track, video track id: %d\n", videotrackId);
		return -1;
	}
	if (audiotrackId == MP4_INVALID_TRACK_ID) {
		log_err("Can't find audio track, audio track id: %d\n", audiotrackId);
		return -1;
	}
#if 1 //TODO platform related
	ptMediaInfo->audioType = -1;
	const char *strAudioName = MP4GetTrackMediaDataName(mp4File, audiotrackId);
	if (0 == strncmp(strAudioName, "mp4a", 4)) {
		ptMediaInfo->audioType = MEDIA_CODEC_AUDIO_AAC;
	} else if (0 == strncmp(strAudioName, "alaw", 4)) {
		ptMediaInfo->audioType = MEDIA_CODEC_AUDIO_G711A;
	}
//	  printf ("audio name: %s audio type: %d\n", strAudioName, ptMediaInfo->audioType);

           ptMediaInfo->videoType = -1;
           const char *strVideoName = MP4GetTrackMediaDataName(mp4File, videotrackId);
      //     log_debug ("video name: %s\n", strVideoName);
	if (0 == strncmp(strVideoName, "hvc1", 4)) {
        #if 0
		ptMediaInfo->videoType = MEDIA_CODEC_VIDEO_HEVC;
            	ret =
            	    get_mp4_h265_vps_sps_pps(mp4File, videotrackId, (unsigned char *)ptMediaInfo->spsppsbuf,
            				     (int *)&ptMediaInfo->spsppslen);
            	if (-1 == ret) {
            		log_err("error to get mp4 vps/sps/pps\n");
            		return -1;
            	}   
        #endif
	} else if (0 == strncmp(strVideoName, "avc1", 4)) {
		ptMediaInfo->videoType = MEDIA_CODEC_VIDEO_H264;
                      ret =
            	    getmp4sps_pps(mp4File, videotrackId, (unsigned char *)ptMediaInfo->spsppsbuf,
            				     (int *)&ptMediaInfo->spsppslen);
            	if (-1 == ret) {
            		log_err("error to get mp4 sps/pps\n");
            		return -1;
            	}                
	}

#endif

	ptMediaInfo->videotimescale = MP4GetTrackTimeScale(mp4File, videotrackId);
	if (ptMediaInfo->videotimescale <= 0) {
		log_err("video timescale is invalid\n");
		return -1;
	}
	ptMediaInfo->audiotimescale = MP4GetTrackTimeScale(mp4File, audiotrackId);
	if (ptMediaInfo->audiotimescale <= 0) {
		log_err("audio timescale is invalid\n");
		return -1;
	}
 //   log_info( "videotimescale:%u, audiotimescale:%u\n", ptMediaInfo->videotimescale, ptMediaInfo->audiotimescale);

	uint32_t numVideoSamples = MP4GetTrackNumberOfSamples(mp4File, videotrackId);
	if (0 == numVideoSamples) {
		log_err("there is no video data in mp4 file\n");
		return -1;
	}
	uint32_t numAudioSamples = MP4GetTrackNumberOfSamples(mp4File, audiotrackId);
	if (0 == numAudioSamples) {
		log_err("there is no audio data in mp4 file\n");
	}
//	log_info("numVideoSamples: %d, numAudioSamples:%d\n", numVideoSamples, numAudioSamples);

         ptMediaInfo->duration = (int)MP4GetDuration(mp4File) /(ptMediaInfo->videotimescale);	
         if (ptMediaInfo->duration <= 0 ) {
                log_err ( "got error duraion: %d\n", duration);
                return -1;
         }
  //        log_debug ("%s, vps_sps_pps len: %d\n", __func__, ptMediaInfo->spsppslen);

           fps = numVideoSamples * ptMediaInfo->videotimescale / MP4GetSampleTime(mp4File, videotrackId, numVideoSamples);
	if (fps > 1000) {
		log_err("mp4 frame rate is incorrect: [%d]\n", fps);
		fps = 20;
	}

           ptMediaInfo->fps = fps;
	ptMediaInfo->audioFrameCnt = numAudioSamples;
	ptMediaInfo->videoFrameCnt = numVideoSamples;

	ptMediaInfo->width = videoWidth;
	ptMediaInfo->height = videoHeight;
	ptMediaInfo->videotrackId = videotrackId;
	ptMediaInfo->audiotrackId = audiotrackId;

	return 0;
}
