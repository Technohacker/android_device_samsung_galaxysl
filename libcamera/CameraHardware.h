/*
**
** Copyright (C) 2009 0xlab.org - http://0xlab.org/
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <utils/threads.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/Log.h>
#include <utils/threads.h>
#include "binder/MemoryBase.h"
#include "binder/MemoryHeapBase.h"

#include <camera/CameraParameters.h>
#include <hardware/camera.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <sys/ioctl.h>
#include <dlfcn.h>

#include "V4L2Camera.h"

#include <math.h>
#include "Exif.h"
#include "ExifCreator.h"

#include "RotationInterface.h"

namespace android {

typedef struct {
    size_t width;
    size_t height;
} supported_resolution;

class CameraHardware {
public:
    virtual void setCallbacks(camera_notify_callback notify_cb,
                              camera_data_callback data_cb,
                              camera_data_timestamp_callback data_cb_timestamp,
                              camera_request_memory get_memory,
                              void* user);
    virtual void        enableMsgType(int32_t msgType);
    virtual void        disableMsgType(int32_t msgType);
    virtual bool        msgTypeEnabled(int32_t msgType);

    virtual status_t setPreviewWindow( struct preview_stream_ops *window);

    virtual status_t    startPreview();
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording();
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const void* opaque);

    virtual status_t    autoFocus();
    virtual status_t    cancelAutoFocus();
    virtual status_t    takePicture();
    virtual status_t    cancelPicture();
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual status_t sendCommand(int32_t cmd, int32_t arg1, int32_t arg2);
    virtual void release();

                        CameraHardware(int CameraID);
    virtual             ~CameraHardware();

    //static wp<CameraHardwareInterface> singleton;

private:
    status_t    startPreviewInternal();
    void stopPreviewInternal();

    static const int kBufferCount = 6;

    class PreviewThread : public Thread {
        CameraHardware* mHardware;
    public:
        PreviewThread(CameraHardware* hw):
            //: Thread(false), mHardware(hw) { }
#ifdef SINGLE_PROCESS
            // In single process mode this thread needs to be a java thread,
            // since we won't be calling through the binder.
            Thread(true),
#else
			// We use Andorid thread
            Thread(false),
#endif
              mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardware *mHardware;
    public:
        AutoFocusThread(CameraHardware *hw): Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAutoFocusThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return true;
        }
    };

    class PictureThread : public Thread {
        CameraHardware *mHardware;
    public:
        PictureThread(CameraHardware *hw):
        Thread(false),
        mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

    void initDefaultParameters(int CameraID);
	void SetDSPKHz(unsigned int KHz);
	int get_kernel_version();
	void CreateExif(unsigned char* pInThumbnailData,int Inthumbsize,unsigned char* pOutExifBuf,int& OutExifSize,int flag);
	void convertFromDecimalToGPSFormat(double arg1,int& arg2,int& arg3,double& arg4);
	int convertToExifLMH(int value, int key);
	double getGPSLatitude() const;
	double getGPSLongitude() const;
	double getGPSAltitude() const;

	/* validating supported size */
	bool validateSize(size_t width, size_t height,
			const supported_resolution *supRes, size_t count);

    static int beginAutoFocusThread(void *cookie);

    static int beginPictureThread(void *cookie);

    camera_request_memory   mRequestMemory;
    preview_stream_ops_t*  mNativeWindow;
    camera_memory_t     *mRecordHeap[kBufferCount];
    camera_memory_t     *mPreviewHeap;
    int mRecordBufferState[kBufferCount];

    // FFC preview resize
    camera_memory_t     *mScaleHeap;
    camera_memory_t     *mFrameScaled;

    mutable Mutex       mLock;           // member property lock
    mutable Mutex       mPreviewLock;    // hareware v4l2 operation lock
    Mutex               mRecordingLock;
    CameraParameters    mParameters;

    mutable Mutex mSkipFrameLock;
            int mSkipFrame;

    V4L2Camera         *mCamera;
	
	NEON_fpo Neon_Rotate;
	NEON_FUNCTION_ARGS* neon_args;
	void* pTIrtn;

	int mPreviewWidth;
	int mPreviewHeight;
	static const char supportedPictureSizes_ffc[];
	static const char supportedPictureSizes_bfc[];
	static const char supportedPreviewSizes_ffc[];
	static const char supportedPreviewSizes_bfc[];
    
	mutable Mutex mFocusLock;
    	mutable Condition mFocusCondition;
    	bool 	 	mExitAutoFocusThread;

    /* used by preview thread to block until it's told to run */
    mutable Condition   mPreviewCondition;
    mutable Condition   mPreviewStoppedCondition;
            bool        mPreviewRunning;
            bool        mPreviewStartDeferred;
            bool        mExitPreviewThread;

    sp<AutoFocusThread> mAutoFocusThread;
            int autoFocusThread();

    /* protected by mLock */
    sp<PreviewThread>   mPreviewThread;
    int         previewThread();
    int         previewThreadWrapper();


    sp<PictureThread> mPictureThread;
            int pictureThread();

    camera_notify_callback     mNotifyCb;
    camera_data_callback       mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    bool                mRecordingEnabled;

			double mPreviousGPSLatitude;
			double mPreviousGPSLongitude;
			double mPreviousGPSAltitude;
			long mPreviousGPSTimestamp;
			struct tm *m_timeinfo;
			char m_gps_date[11];
			time_t m_gps_time;
			int m_gpsHour;
			int m_gpsMin;
			int m_gpsSec;
			int mThumbnailWidth;
			int mThumbnailHeight;
			int mPreviousSceneMode;
			int mPreviousFlashMode;
			int mPreviousBrightness;
			int mPreviousExposure;
			int mPreviousZoom;
			int mPreviousISO;
			int mPreviousContrast;
			int mPreviousSaturation;
			int mPreviousSharpness;
			int mPreviousMetering;
			bool isStart_scaler;
    static gralloc_module_t const* mGrallocHal;
};

}; // namespace android
extern "C" {
int scale_init(int inWidth, int inHeight, int outWidth, int outHeight, int inFmt, int outFmt);
int scale_deinit();
int scale_process(void* inBuffer, int inWidth, int inHeight, void* outBuffer, int outWidth, int outHeight, int rotation, int fmt, float zoom);
}

extern "C" {
int ColorConvert_Init(int , int , int);
int ColorConvert_Deinit();
int ColorConvert_Process(char *, char *);
int encodeImage(void* outputBuffer, void *inputBuffer, int width, int height, int quality, unsigned char* pExifBuf, int ExifSize, int ThumbWidth, int ThumbHeight);
void Neon_Convert_yuv422_to_NV21(unsigned char * aSrcBufPtr, unsigned char * aDstBufPtr,unsigned int aFramewidth,unsigned int aFrameHeight);
void Neon_Convert_yuv422_to_NV12(unsigned char * aSrcBufPtr, unsigned char * aDstBufPtr,unsigned int aFramewidth,unsigned int aFrameHeight);
void Neon_Convert_yuv422_to_YUV420P(unsigned char * aSrcBufPtr, unsigned char * aDstBufPtr,unsigned int aFramewidth,unsigned int aFrameHeight);
void UYVYToI420(unsigned char * aSrcBufPtr, unsigned char * aDstBufPtr,unsigned int aFramewidth,unsigned int aFrameHeight);
}
extern "C" {
int scale_init(int inWidth, int inHeight, int outWidth, int outHeight, int inFmt, int outFmt);
int scale_deinit();
int scale_process(void* inBuffer, int inWidth, int inHeight, void* outBuffer, int outWidth, int outHeight, int rotation, int fmt, float zoom);
}
#endif
