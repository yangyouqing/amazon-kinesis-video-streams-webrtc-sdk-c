#include "Samples.h"
#include "mp4streamfetch.h"
extern PSampleConfiguration gSampleConfiguration;

// #define VERBOSE

INT32 main(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 frameSize;
    PSampleConfiguration pSampleConfiguration = NULL;
    SignalingClientMetrics signalingClientMetrics;
    PCHAR pChannelName;
    signalingClientMetrics.version = SIGNALING_CLIENT_METRICS_CURRENT_VERSION;

    SET_INSTRUMENTED_ALLOCATORS();

#ifndef _WIN32
    signal(SIGINT, sigintHandler);
#endif

    // do trickleIce by default
    printf("[KVS Master] Using trickleICE by default\n");

#ifdef IOT_CORE_ENABLE_CREDENTIALS
    CHK_ERR((pChannelName = getenv(IOT_CORE_THING_NAME)) != NULL, STATUS_INVALID_OPERATION, "AWS_IOT_CORE_THING_NAME must be set");
#else
    pChannelName = argc > 1 ? argv[1] : SAMPLE_CHANNEL_NAME;
#endif

    retStatus = createSampleConfiguration(pChannelName, SIGNALING_CHANNEL_ROLE_TYPE_MASTER, TRUE, TRUE, &pSampleConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] createSampleConfiguration(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

    printf("[KVS Master] Created signaling channel %s\n", pChannelName);

    if (pSampleConfiguration->enableFileLogging) {
        retStatus =
            createFileLogger(FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] createFileLogger(): operation returned status code: 0x%08x \n", retStatus);
            pSampleConfiguration->enableFileLogging = FALSE;
        }
    }

    // Set the audio and video handlers
    #ifndef MEDIA_SEND_BY_SINGLE_THREAD
    pSampleConfiguration->audioSource = sendAudioPackets;
    pSampleConfiguration->videoSource = sendVideoPackets;
    #else
    pSampleConfiguration->mediaSource = sendAvPackets;
    #endif
    pSampleConfiguration->receiveAudioVideoSource = sampleReceiveVideoFrame;
    pSampleConfiguration->onDataChannel = onDataChannel;
    pSampleConfiguration->mediaType = SAMPLE_STREAMING_VIDEO_ONLY;
    printf("[KVS Master] Finished setting audio and video handlers\n");

#if 0
    // Check if the samples are present

    retStatus = readFrameFromDisk(NULL, &frameSize, "./h264SampleFrames/frame-0001.h264");
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Checked sample video frame availability....available\n");

    retStatus = readFrameFromDisk(NULL, &frameSize, "./opusSampleFrames/sample-001.opus");
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Checked sample audio frame availability....available\n");
#endif
    // Initialize KVS WebRTC. This must be done before anything else, and must only be done once.
    retStatus = initKvsWebRtc();
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] initKvsWebRtc(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] KVS WebRTC initialization completed successfully\n");

    pSampleConfiguration->signalingClientCallbacks.messageReceivedFn = signalingMessageReceived;

    strcpy(pSampleConfiguration->clientInfo.clientId, SAMPLE_MASTER_CLIENT_ID);

    retStatus = createSignalingClientSync(&pSampleConfiguration->clientInfo, &pSampleConfiguration->channelInfo,
                                          &pSampleConfiguration->signalingClientCallbacks, pSampleConfiguration->pCredentialProvider,
                                          &pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] createSignalingClientSync(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Signaling client created successfully\n");

#if 0
    // Enable the processing of the messages
    retStatus = signalingClientFetchSync(pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] signalingClientFetchSync(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
#endif

    retStatus = signalingClientConnectSync(pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] signalingClientConnectSync(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Signaling client connection to socket established\n");

    gSampleConfiguration = pSampleConfiguration;

    printf("[KVS Master] Channel %s set up done \n", pChannelName);

    // Checking for termination
    retStatus = sessionCleanupWait(pSampleConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] sessionCleanupWait(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

    printf("[KVS Master] Streaming session terminated\n");

CleanUp:

    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] Terminated with status code 0x%08x\n", retStatus);
    }

    printf("[KVS Master] Cleaning up....\n");
    if (pSampleConfiguration != NULL) {
        // Kick of the termination sequence
        ATOMIC_STORE_BOOL(&pSampleConfiguration->appTerminateFlag, TRUE);

        if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->sampleConfigurationObjLock)) {
            MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
        }

        // Cancel the media thread
        if (pSampleConfiguration->mediaThreadStarted) {
            DLOGD("Canceling media thread");
            THREAD_CANCEL(pSampleConfiguration->mediaSenderTid);
        }

        if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->sampleConfigurationObjLock)) {
            MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
        }

        if (pSampleConfiguration->mediaSenderTid != INVALID_TID_VALUE) {
            THREAD_JOIN(pSampleConfiguration->mediaSenderTid, NULL);
        }

        if (pSampleConfiguration->enableFileLogging) {
            freeFileLogger();
        }
        retStatus = signalingClientGetMetrics(pSampleConfiguration->signalingClientHandle, &signalingClientMetrics);
        if (retStatus == STATUS_SUCCESS) {
            logSignalingClientStats(&signalingClientMetrics);
        } else {
            printf("[KVS Master] signalingClientGetMetrics() operation returned status code: 0x%08x\n", retStatus);
        }
        retStatus = freeSignalingClient(&pSampleConfiguration->signalingClientHandle);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] freeSignalingClient(): operation returned status code: 0x%08x", retStatus);
        }

        retStatus = freeSampleConfiguration(&pSampleConfiguration);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] freeSampleConfiguration(): operation returned status code: 0x%08x", retStatus);
        }
    }
    printf("[KVS Master] Cleanup done\n");

    RESET_INSTRUMENTED_ALLOCATORS();

    // https://www.gnu.org/software/libc/manual/html_node/Exit-Status.html
    // We can only return with 0 - 127. Some platforms treat exit code >= 128
    // to be a success code, which might give an unintended behaviour.
    // Some platforms also treat 1 or 0 differently, so it's better to use
    // EXIT_FAILURE and EXIT_SUCCESS macros for portability.
    return STATUS_FAILED(retStatus) ? EXIT_FAILURE : EXIT_SUCCESS;
}

STATUS readFrameFromDisk(PBYTE pFrame, PUINT32 pSize, PCHAR frameFilePath)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 size = 0;

    if (pSize == NULL) {
        printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    size = *pSize;

    // Get the size and read into frame
    retStatus = readFile(frameFilePath, TRUE, pFrame, &size);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] readFile(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

CleanUp:

    if (pSize != NULL) {
        *pSize = (UINT32) size;
    }

    return retStatus;
}

PVOID sendVideoPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;
    RtcEncoderStats encoderStats;
    Frame frame;
    UINT32 fileIndex = 0, frameSize;
    CHAR filePath[MAX_PATH_LEN + 1];
    STATUS status;
    UINT32 i;
    UINT64 startTime, lastFrameTime, elapsed;
    MEMSET(&encoderStats, 0x00, SIZEOF(RtcEncoderStats));

    if (pSampleConfiguration == NULL) {
        printf("[KVS Master] sendVideoPackets(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    frame.presentationTs = 0;
    startTime = GETTIME();
    lastFrameTime = startTime;

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) {
        fileIndex = fileIndex % NUMBER_OF_H264_FRAME_FILES + 1;
        snprintf(filePath, MAX_PATH_LEN, "./h264SampleFrames/frame-%04d.h264", fileIndex);

        retStatus = readFrameFromDisk(NULL, &frameSize, filePath);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
            goto CleanUp;
        }

        // Re-alloc if needed
        if (frameSize > pSampleConfiguration->videoBufferSize) {
            pSampleConfiguration->pVideoFrameBuffer = (PBYTE) MEMREALLOC(pSampleConfiguration->pVideoFrameBuffer, frameSize);
            if (pSampleConfiguration->pVideoFrameBuffer == NULL) {
                printf("[KVS Master] Video frame Buffer reallocation failed...%s (code %d)\n", strerror(errno), errno);
                printf("[KVS Master] MEMREALLOC(): operation returned status code: 0x%08x \n", STATUS_NOT_ENOUGH_MEMORY);
                goto CleanUp;
            }

            pSampleConfiguration->videoBufferSize = frameSize;
        }

        frame.frameData = pSampleConfiguration->pVideoFrameBuffer;
        frame.size = frameSize;

        retStatus = readFrameFromDisk(frame.frameData, &frameSize, filePath);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
            goto CleanUp;
        }

        // based on bitrate of samples/h264SampleFrames/frame-*
        encoderStats.width = 640;
        encoderStats.height = 480;
        encoderStats.targetBitrate = 262000;
        frame.presentationTs += SAMPLE_VIDEO_FRAME_DURATION;

        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            status = writeFrame(pSampleConfiguration->sampleStreamingSessionList[i]->pVideoRtcRtpTransceiver, &frame);
            encoderStats.encodeTimeMsec = 4; // update encode time to an arbitrary number to demonstrate stats update
            updateEncoderStats(pSampleConfiguration->sampleStreamingSessionList[i]->pVideoRtcRtpTransceiver, &encoderStats);
            if (status != STATUS_SRTP_NOT_READY_YET) {
                if (status != STATUS_SUCCESS) {
#ifdef VERBOSE
                    printf("writeFrame() failed with 0x%08x\n", status);
#endif
                }
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);

        // Adjust sleep in the case the sleep itself and writeFrame take longer than expected. Since sleep makes sure that the thread
        // will be paused at least until the given amount, we can assume that there's no too early frame scenario.
        // Also, it's very unlikely to have a delay greater than SAMPLE_VIDEO_FRAME_DURATION, so the logic assumes that this is always
        // true for simplicity.
        elapsed = lastFrameTime - startTime;
        THREAD_SLEEP(SAMPLE_VIDEO_FRAME_DURATION - elapsed % SAMPLE_VIDEO_FRAME_DURATION);
        lastFrameTime = GETTIME();
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    return (PVOID) (ULONG_PTR) retStatus;
}

PVOID sendAudioPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;
    Frame frame;
    UINT32 fileIndex = 0, frameSize;
    CHAR filePath[MAX_PATH_LEN + 1];
    UINT32 i;
    STATUS status;

    if (pSampleConfiguration == NULL) {
        printf("[KVS Master] sendAudioPackets(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    frame.presentationTs = 0;

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) {
        fileIndex = fileIndex % NUMBER_OF_OPUS_FRAME_FILES + 1;
        snprintf(filePath, MAX_PATH_LEN, "./opusSampleFrames/sample-%03d.opus", fileIndex);

        retStatus = readFrameFromDisk(NULL, &frameSize, filePath);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
            goto CleanUp;
        }

        // Re-alloc if needed
        if (frameSize > pSampleConfiguration->audioBufferSize) {
            pSampleConfiguration->pAudioFrameBuffer = (UINT8*) MEMREALLOC(pSampleConfiguration->pAudioFrameBuffer, frameSize);
            if (pSampleConfiguration->pAudioFrameBuffer == NULL) {
                printf("[KVS Master] Audio frame Buffer reallocation failed...%s (code %d)\n", strerror(errno), errno);
                printf("[KVS Master] MEMREALLOC(): operation returned status code: 0x%08x \n", STATUS_NOT_ENOUGH_MEMORY);
                goto CleanUp;
            }
        }

        frame.frameData = pSampleConfiguration->pAudioFrameBuffer;
        frame.size = frameSize;

        retStatus = readFrameFromDisk(frame.frameData, &frameSize, filePath);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
            goto CleanUp;
        }

        frame.presentationTs += SAMPLE_AUDIO_FRAME_DURATION;

        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            status = writeFrame(pSampleConfiguration->sampleStreamingSessionList[i]->pAudioRtcRtpTransceiver, &frame);
            if (status != STATUS_SRTP_NOT_READY_YET) {
                if (status != STATUS_SUCCESS) {
#ifdef VERBOSE
                    printf("writeFrame() failed with 0x%08x\n", status);
#endif
                }
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);
        THREAD_SLEEP(SAMPLE_AUDIO_FRAME_DURATION);
    }

CleanUp:

    return (PVOID) (ULONG_PTR) retStatus;
}

#define MAX_VIDEO_FRAME_LEN (255 * 1024)
#define MAX_AUDIO_FRAME_LEN (8 * 1024)
#define MAX_VIDEO_SPS_PPS_LEN (4096 + 4096)	

static FILE *mp4File;
TMediaInfo tMediaInfo = { 0 };
TVideoFrameInfo tVideoFrame = { 0 };    
TAudioFrameInfo tAudioFrame = { 0 };
static uint32_t videosampleId = 1;
static uint32_t audiosampleId = 1;

bool av_send_ctrl(bool firstframe, uint64_t ts) 
{
#define ADVANCE_TIME 1000    // in ms
    struct timeval t_now;
    gettimeofday(&t_now, NULL);
    uint64_t now = ((uint64_t)t_now.tv_sec) * 1000 + t_now.tv_usec / 1000;    
    static uint64_t start_time = 0;
    static uint64_t start_ts = 0;
    #define ADVANCE_TIME 1000       // ms
    if (firstframe) {
        start_time = now;
        start_ts = ts;
    }

    if (ts < start_ts) {
        printf("error ts\n");
        return false;
    }

    if (now + ADVANCE_TIME - start_time > ts - start_ts) {
      //  printf ("sysdiff: %llu, mediadiff: %llu\n", now - start_time, ts - start_ts);
        return true;
    }
    
    return false;
}

int mp4playback_init() 
{
    #define MP4_FILE_NAME "../test/mijia_video.mp4"
    if (NULL != mp4File) {
        MP4Close(mp4File, 0);
    }
    
    mp4File = (FILE *)MP4Read(MP4_FILE_NAME);
    if (NULL == mp4File) {
        printf ("failed to open file %s\n", MP4_FILE_NAME);
        return -1;
    }
    
    if (NULL == tVideoFrame.buf) {
    	tVideoFrame.buf = (char *)malloc(MAX_VIDEO_FRAME_LEN);
    	if (NULL == tVideoFrame.buf) {
    	        printf("%s, malloc memeory failed\n", __func__);
                       return -1;
    	}
    }

    if (-1 == GetMp4Info(mp4File, &tMediaInfo)) {
		return -1;
	}

    showmediainfo(&tMediaInfo);
	tVideoFrame.ptMediaInfo = &tMediaInfo;
    tAudioFrame.ptMediaInfo = &tMediaInfo;
}

int getnextframe(int *isvideo)
{
    uint64_t pts = 0;
    uint64_t pts2 = 0;
    
    static	uint32_t videosampleId = 1;
    static	uint32_t audiosampleId = 1;
    static bool firstkeyframe = true;

    static uint64_t ats = -1;
    static uint64_t vts = -1;

    if (audiosampleId >= tMediaInfo.audioFrameCnt && videosampleId >= tMediaInfo.videoFrameCnt) {
        #if 0
        printf ("fin to send av\n");
        struct itimerspec nval = {0};    
    	timerfd_settime(fd, 0, &nval, NULL);
        #else 
        mp4playback_init();
        audiosampleId = 1;
        videosampleId = 1;
        firstkeyframe = true;
        ats = -1;
        vts = -1;
        #endif
        return -1;
    }

    if (firstkeyframe) {
        videosampleId = SeektoKeyFrame(mp4File, tMediaInfo.videotrackId, &tVideoFrame);
        tVideoFrame.seq = videosampleId;
        pts = tVideoFrame.pts;
      //  pts2 = av_rescale(pts, 90000, tMediaInfo.videotimescale);
        int tmp = 0;
       // tsmuxer_encode_vframe(tVideoFrame.buf, tVideoFrame.buflen, pts2, &tVideoFrame.tsbuf, &tVideoFrame.tslen, tVideoFrame.iskey);
        vts = tVideoFrame.timestamp;

        while (ats < vts || -1 == ats) {
            GetAudioFrameDataFromMp4(mp4File, tMediaInfo.audiotrackId, audiosampleId,
								 &tAudioFrame);
            ats = tAudioFrame.timestamp;
            audiosampleId++;
        }

        pts = tAudioFrame.pts;
     //   pts2 = av_rescale(pts, 90000, tMediaInfo.audiotimescale);
     //   tsmuxer_encode_aframe(tAudioFrame.buf, tAudioFrame.buflen, pts2, &tAudioFrame.tsbuf, &tAudioFrame.tslen);
        av_send_ctrl(true, tAudioFrame.timestamp);
        firstkeyframe = false;
    }

    if (-1 == vts) {
        GetVideoFrameInfoFrommp4(mp4File, tMediaInfo.videotrackId, videosampleId, &tVideoFrame);
        tVideoFrame.seq = videosampleId;
        pts = tVideoFrame.pts;
     //   pts2 = av_rescale(pts, 90000, tMediaInfo.videotimescale);
     //   tsmuxer_encode_vframe(tVideoFrame.buf, tVideoFrame.buflen, pts2, &tVideoFrame.tsbuf, &tVideoFrame.tslen, tVideoFrame.iskey);
        
 	  //  printf("vSampleid: %d, ts:%d, pts: %llu, pts2:%llu, size: %d\n", videosampleId, tVideoFrame.timestamp,
      //      pts, pts2, tVideoFrame.buflen);
        vts = tVideoFrame.timestamp;
    }

    if (-1 == ats) {
        GetAudioFrameDataFromMp4(mp4File, tMediaInfo.audiotrackId, audiosampleId,
								 &tAudioFrame);
        pts = tAudioFrame.pts;
      //  pts2 = av_rescale(pts, 90000, tMediaInfo.audiotimescale);
      //  tsmuxer_encode_aframe(tAudioFrame.buf, tAudioFrame.buflen, pts2,  &tAudioFrame.tsbuf, &tAudioFrame.tslen);

 	//    printf("aSampleid: %d, ts:%d, pts: %llu, pts2:%llu, size: %d\n", audiosampleId, tAudioFrame.timestamp,
    //        pts, pts2, tAudioFrame.buflen);
        ats = tAudioFrame.timestamp;
    }

    int snd_ret = 0;

    static uint64_t ts = 0;
    static uint64_t last_ts = 0;

    static uint64_t last_sent_time = 0;

 //   printf ("ats: %llu, vts:%llu\n", ats, vts);
    
    if (-1 != ats && -1 != vts) {
        // when to send video
        if (vts < ats) {
            videosampleId++;
            vts = -1;
            *isvideo = 1;
            return 0;
        }

        // when to send audio
        if (av_send_ctrl(false, tAudioFrame.timestamp)) {
            audiosampleId++;
            ats = -1;
            *isvideo = 0;
            return 0;
        }
    }
    return -1;
}

PVOID sendAvPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;
    Frame frame;
    UINT32 frameSize;
    UINT32 i;
    STATUS status;

    int isvideo = 0;
    int fetch_ret = 0;


    if (pSampleConfiguration == NULL) {
        printf("[KVS Master] sendAudioPackets(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    frame.presentationTs = 0;

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) {

        fetch_ret = getnextframe(&isvideo);
        if (fetch_ret < 0) {
           continue;
        }

        if (isvideo) {
            frame.frameData = tVideoFrame.buf;
            frame.size = tVideoFrame.buflen;
            frame.presentationTs = tVideoFrame.pts;
        } else {
            continue;
        }
        
        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            status = writeFrame(pSampleConfiguration->sampleStreamingSessionList[i]->pVideoRtcRtpTransceiver, &frame);
            if (status != STATUS_SRTP_NOT_READY_YET) {
                if (status != STATUS_SUCCESS) {
#ifdef VERBOSE
                    printf("writeFrame() failed with 0x%08x\n", status);
#endif
                }
            } else {
                printf ("frame seq: %d is not ready yet\n", tVideoFrame.seq);
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);
        static int last_vseq = 0;
        if (last_vseq != tVideoFrame.seq - 1) {
            printf ("warning , seq is not continuous seq: %d, last_seq: %d\n", tVideoFrame.seq, last_vseq);
        }
        last_vseq = tVideoFrame.seq;
        //THREAD_SLEEP(SAMPLE_AUDIO_FRAME_DURATION);
    }

CleanUp:

    return (PVOID) (ULONG_PTR) retStatus;
}

PVOID sampleReceiveVideoFrame(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) args;
    if (pSampleStreamingSession == NULL) {
        printf("[KVS Master] sampleReceiveVideoFrame(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    retStatus = transceiverOnFrame(pSampleStreamingSession->pVideoRtcRtpTransceiver, (UINT64) pSampleStreamingSession, sampleFrameHandler);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] transceiverOnFrame(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

CleanUp:

    return (PVOID) (ULONG_PTR) retStatus;
}
