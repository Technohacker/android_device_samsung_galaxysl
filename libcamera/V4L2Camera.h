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

#ifndef _V4L2CAMERA_H
#define _V4L2CAMERA_H

#define NB_BUFFER 6
#define DEFAULT_FRAME_RATE 30

#define STATE_PREVIEW      0
#define STATE_PICTURE      1

#define MODE_CAMERA        1
#define MODE_CAMCORDER     2
#define MODE_VT            3

#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <linux/videodev.h>
#include <utils/Log.h>

#include <hardware/camera.h>

#include "media.h"
#include "v4l2-mediabus.h"
#include "v4l2-subdev.h"

#include <linux/videodev2.h>
#define LOG_FUNCTION_START    ALOGD("%d: %s() ENTER", __LINE__, __FUNCTION__);
#define LOG_FUNCTION_EXIT    ALOGD("%d: %s() EXIT", __LINE__, __FUNCTION__);

/* TODO: enable once resizer driver is up 
#define _OMAP_RESIZER_ 0*/

#ifdef _OMAP_RESIZER_
#include "saResize.h"
#endif //_OMAP_RESIZER_

namespace android {

struct vdIn {
    struct v4l2_capability cap;
    struct v4l2_format format;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers rb;
    void *mem[NB_BUFFER];
    bool isStreaming;
    int width;
    int height;
    int formatIn;
    int framesizeIn;
#ifdef _OMAP_RESIZER_
	int resizeHandle;
#endif //_OMAP_RESIZER_
};
struct mdIn {
	int media_fd;
	int input_source;
	struct media_entity_desc entity[20];
	int video;
	int ccdc;
	int tvp5146;
	int mt9t111;
	int mt9v113;
	unsigned int num_entities;
};

class V4L2Camera {

public:
    V4L2Camera();
    ~V4L2Camera();

    int Open (const char *device);
    int Configure(int width,int height,int pixelformat,int fps,int cam_mode);
    void Close ();
    void reset_links(const char *device);
    int Open_media_device(const char *device);

    int BufferMap (int cam_mode);
    int init_parm();
    void Uninit (int cam_mode);

    int StartStreaming (int cam_mode);
    int StopStreaming (int cam_mode);

    int setAutofocus();
    int getAutoFocusResult();
    int cancelAutofocus();
    int setObjectPosition(int x, int y);
    int setTouchAFStartStop(int start_stop);
    int setZoom(int zoom_level);
    int setWhiteBalance(int white_balance);
    int setFramerate(int framerate,int cam_mode);
    int setBrightness(int brightness);
    int getBrightness(void);
    int setSceneMode(int scene_mode);
    int getSceneMode();
    int setFocusMode(int focus_mode);
    int setAEAWBLockUnlock(int ae_lockunlock, int awb_lockunlock);
    int SetCameraFlip(bool isCapture);
	int GetJpegImageSize();
	int GetThumbNailOffset();
	int GetYUVOffset();
	int GetThumbNailDataSize();
	int setExifOrientationInfo(int orientationInfo);
	int getOrientation();
	void stopPreview();
	int GetJPEG_Capture_Width();
	int GetJPEG_Capture_Height();
	int GetCamera_version();
	void getExifInfoFromDriver(v4l2_exif* exifobj);
	int getWhiteBalance();
	int setMetering(int metering_value);
	int setISO(int iso_value);
	int getISO(void);
	int getMetering(void);
	int setContrast(int contrast_value);
	int getContrast(void);
	int getSharpness(void);
	int setSharpness(int sharpness_value);
	int setSaturation(int saturation_value);
	int getSaturation(void);
	int setImageEffect(int);
	int getImageEffect(void);
	int setJpegQuality(int jpeg_quality);
	int getJpegQuality(void);
	int setCamMode(int cam_mode);
	int getCamMode(void);


    void * GrabFrame (int& index);
    void ReleaseFrame (int index);
    void GrabRawFrame(void *previewBuffer, unsigned int width, unsigned int height);
    void * GrabJpegFrame (int& mfilesize,bool IsFrontCam);
    void convert(unsigned char *buf, unsigned char *rgb, int width, int height);

private:
    struct vdIn *videoIn;
    struct mdIn *mediaIn;
    int camHandle;
    int m_flag_init;  
    int mZoomLevel;
    int mWhiteBalance;
    int mFocusMode;
    int mSceneMode;
    int m_exif_orientation;
    int mISO;
    int mMetering;
    int mContrast;
    int mSharpness;
    int mSaturation;
    int mBrightness;
    int mAutofocusRunning;
    int mEffect;
    int mJpegQuality;
    int mCamMode;

    int nQueued;
    int nDequeued;

enum AE_AWB_LOCK_UNLOCK
{
AE_UNLOCK_AWB_UNLOCK = 0,
AE_LOCK_AWB_UNLOCK,
AE_UNLOCK_AWB_LOCK,
AE_LOCK_AWB_LOCK,
AE_AWB_MAX
};
};

}; // namespace android
#endif
