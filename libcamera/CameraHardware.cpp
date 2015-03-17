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

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include "CameraUtils.h"
#include "converter.h"
#include <fcntl.h>
#include <sys/mman.h>

#include <cutils/native_handle.h>
#include <hal_public.h>
#include <ui/GraphicBufferMapper.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define VIDEO_DEVICE_2      "/dev/video5"
#define VIDEO_DEVICE_0      "/dev/video0"
#define MEDIA_DEVICE        "/dev/media0"
#define PREVIEW_WIDTH       640
#define PREVIEW_HEIGHT      480
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV
//Define Cameras
#define CAMERA_BF 0 //Back Camera
#define CAMERA_FF 1

#define PIX_YUV422I 0
#define HAL_PIXEL_FORMAT_YUV     0x1B

#define CAMHAL_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
                             GRALLOC_USAGE_HW_RENDER | \
                             GRALLOC_USAGE_SW_READ_RARELY | \
                             GRALLOC_USAGE_SW_WRITE_NEVER

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif
#define MAX_STR_LEN 35
#define EXIF_FILE_SIZE 65535

#define DSP3630_KHZ_MIN 260000
#define DSP3630_KHZ_MAX 800000

#define BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR "0.10,1.20,Infinity"
#define BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR "0.10,0.20,Infinity"
#define BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR "0.10,1.20,Infinity"
#define FRONT_CAMERA_FOCUS_DISTANCES_STR "0.20,0.25,Infinity"
const char FOCUS_MODE_FACEDETECTION[] = "facedetect";

#define MIN_INTERVAL 35000000
#define MAX_INTERVAL 60000000

#include <cutils/properties.h>
#ifndef UNLIKELY
#define UNLIKELY(exp) (__builtin_expect( (exp) != 0, false ))
#endif
static int mDebugFps = 0;
static int mCameraID = 0;
int version = 0;

namespace android {

static int buffersQueued = 0;

static nsecs_t mLastShownTime = 0;

/* 29/12/10 : preview/picture size validation logic */
const char CameraHardware::supportedPictureSizes_ffc [] = "640x480";
const char CameraHardware::supportedPictureSizes_bfc [] = "2560x1920,2560x1536,2048x1536,2048x1232,1600x1200,1600x960,800x480,640x480";
const char CameraHardware::supportedPreviewSizes_ffc [] = "640x480";
const char CameraHardware::supportedPreviewSizes_bfc [] = "1280x720,800x480,720x480,640x480,352x288";
gralloc_module_t const* CameraHardware::mGrallocHal;

CameraHardware::CameraHardware(int CameraID)
                  : mParameters(),
                    mCamera(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mRecordingEnabled(false),
                    isStart_scaler(false)
{
    /* create camera */
    mCamera = new V4L2Camera();
    version = get_kernel_version();
    mCameraID = CameraID;
    int ret = 0;

    mPreviewHeap = NULL;
    mScaleHeap = NULL;
    mFrameScaled = NULL;

    // Disable ISP resizer (use DSS resizer)
    system("echo 0 > "
            "/sys/devices/platform/dsscomp/isprsz/enable");

    if (CameraID == CAMERA_FF) {
        mCamera->Open(VIDEO_DEVICE_2);
        if (scale_init(PREVIEW_WIDTH, PREVIEW_HEIGHT, PREVIEW_WIDTH, PREVIEW_HEIGHT, PIX_YUV422I, PIX_YUV422I) < 0)
            ALOGE("Unable to initialize OMX Scaler");
        else
            isStart_scaler = true;
    } else {
        mCamera->Open(VIDEO_DEVICE_0);
    }

    initDefaultParameters(CameraID);
    mNativeWindow = NULL;
    for (int i = 0; i < NB_BUFFER; i++) {
        mRecordHeap[i] = NULL;
        mRecordBufferState[i] = 0;
    }

    if (!mGrallocHal) {
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t**)&mGrallocHal);
        if (ret)
            ALOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
    }

    mExitAutoFocusThread = false;
    mAutoFocusThread = new AutoFocusThread(this);
    mPictureThread = new PictureThread(this);

    mExitPreviewThread = false;
    /* whether the PreviewThread is active in preview or stopped.  we
     * create the thread but it is initially in stopped state.
     */
    mPreviewRunning = false;
    mPreviewStartDeferred = false;
    mPreviewThread = new PreviewThread(this);

    Neon_Rotate = NULL;
    neon_args = NULL;
    pTIrtn = NULL;
    const char* error;

    //Get the handle of rotation shared library.

    pTIrtn = dlopen("librotation.so", RTLD_LOCAL | RTLD_LAZY);
    if (!pTIrtn) {
        ALOGE("Open NEON Rotation Library Failed \n");
    }

    Neon_Rotate = (NEON_fpo)dlsym(pTIrtn, "Neon_RotateCYCY");

    if ((error = dlerror()) != NULL) {
        ALOGE("Couldnot find  Neon_RotateCYCY symbol\n");
        dlclose(pTIrtn);
        pTIrtn = NULL;
    }
    if (!neon_args) {
        neon_args   = (NEON_FUNCTION_ARGS*)malloc(sizeof(NEON_FUNCTION_ARGS));
    }

    /* whether prop "debug.camera.showfps" is enabled or not */
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.showfps", value, "0");
    mDebugFps = atoi(value);
    ALOGD_IF(mDebugFps, "showfps enabled");
}

void CameraHardware::SetDSPKHz(unsigned int KHz)
{
    char command[100];
    sprintf(command, "echo %u > /sys/power/dsp_freq", KHz);
    system(command);
}

void CameraHardware::initDefaultParameters(int CameraID)
{
    CameraParameters p;
    String8 parameterString;

    if (CameraID == CAMERA_FF) {
        p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
        p.setPictureSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_JPEG_QUALITY, 100);
        p.set("picture-size-values", CameraHardware::supportedPictureSizes_ffc);

        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, CameraHardware::supportedPictureSizes_ffc);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, CameraHardware::supportedPreviewSizes_ffc);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV420SP);
        p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV422I);

        parameterString = CameraParameters::FOCUS_MODE_FIXED;
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              parameterString.string());
        p.set(CameraParameters::KEY_FOCUS_MODE,
              CameraParameters::FOCUS_MODE_FIXED);
        p.set(CameraParameters::KEY_FOCUS_DISTANCES,
              FRONT_CAMERA_FOCUS_DISTANCES_STR);
        p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
              "160x120,0x0");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15");
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(7500,30000)");
        p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "7500,30000");

        p.set(CameraParameters::KEY_FOCAL_LENGTH, "0.9");

        p.setPreviewSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        p.setPreviewFrameRate(15);

    } else {
        p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
        p.setPictureSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_JPEG_QUALITY, 100);
        p.set("picture-size-values", CameraHardware::supportedPictureSizes_bfc);

        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, CameraHardware::supportedPictureSizes_bfc);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, CameraHardware::supportedPreviewSizes_bfc);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV420SP);
        p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV422I);

        parameterString = CameraParameters::FOCUS_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::FOCUS_MODE_MACRO);
        parameterString.append(",");
        parameterString.append(FOCUS_MODE_FACEDETECTION);
        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
              parameterString.string());

        p.set(CameraParameters::KEY_FOCUS_MODE,
              CameraParameters::FOCUS_MODE_AUTO);
        p.set(CameraParameters::KEY_FOCUS_DISTANCES,
              BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
        p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,
              "640x480,0x0");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "640");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "480");
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "30");
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
        p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");

        p.set(CameraParameters::KEY_FOCAL_LENGTH, "3.43");

        p.setPreviewSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
        p.setPreviewFrameRate(30);

        // touch focus
        p.set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS, "1");
        p.set(CameraParameters::KEY_FOCUS_AREAS, "(0,0,0,0,0)");

        // zoom
        p.set(CameraParameters::KEY_ZOOM, "0");
        p.set(CameraParameters::KEY_MAX_ZOOM, "12");
        p.set(CameraParameters::KEY_ZOOM_RATIOS, "100,125,150,175,200,225,250,275,300,325,350,375,400");
        p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");

        // scene
        parameterString = CameraParameters::SCENE_MODE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_PORTRAIT);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_LANDSCAPE);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_NIGHT);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_BEACH);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SNOW);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SUNSET);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_FIREWORKS);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_SPORTS);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_PARTY);
        parameterString.append(",");
        parameterString.append(CameraParameters::SCENE_MODE_CANDLELIGHT);
        parameterString.append(",");
        parameterString.append("backlight,dusk-dawn,text,fall-color");
        p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,
              parameterString.string());

        // effect
        parameterString = CameraParameters::EFFECT_NONE;
        parameterString.append(",");
        parameterString.append(CameraParameters::EFFECT_MONO);
        parameterString.append(",");
        parameterString.append(CameraParameters::EFFECT_NEGATIVE);
        parameterString.append(",");
        parameterString.append(CameraParameters::EFFECT_SEPIA);
        parameterString.append(",");
        parameterString.append(CameraParameters::EFFECT_AQUA);
        parameterString.append(",");
        parameterString.append("sharpen,purple,green-tint,blue-tint,pink,"
                               "yellow,red-tint,mono,antique");
        p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, parameterString.string());

        // whitebalance
        parameterString = CameraParameters::WHITE_BALANCE_AUTO;
        parameterString.append(",");
        parameterString.append(CameraParameters::WHITE_BALANCE_INCANDESCENT);
        parameterString.append(",");
        parameterString.append(CameraParameters::WHITE_BALANCE_FLUORESCENT);
        parameterString.append(",");
        parameterString.append(CameraParameters::WHITE_BALANCE_DAYLIGHT);
        parameterString.append(",");
        parameterString.append(CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
        p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,
              parameterString.string());
    }

    p.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);

    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "4");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-4");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.5");

    p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

    p.set(p.KEY_ROTATION, "0");

    p.set(p.KEY_GPS_LATITUDE, "0");
    p.set(p.KEY_GPS_LONGITUDE, "0");
    p.set(p.KEY_GPS_ALTITUDE, "0");
    p.set(p.KEY_GPS_TIMESTAMP, "0");
    p.set(p.KEY_GPS_PROCESSING_METHOD, "GPS");

    p.set("iso-values", "auto,ISO50,ISO100,ISO200,ISO400,ISO800,ISO1600");
    p.set("iso", "auto");

    //Extra Parameters - GalaxySL
    mCamera->setContrast(CONTRAST_DEFAULT);
    if (CameraID == CAMERA_BF) {
        mCamera->setMetering(METERING_CENTER);
        mCamera->setSharpness(SHARPNESS_DEFAULT);
        mCamera->setSaturation(SATURATION_DEFAULT);
    }

    if (setParameters(p) != NO_ERROR) {
        ALOGE("Failed to set default parameters?!");
    }
    return;
}

int CameraHardware::get_kernel_version()
{
    char *verstring, *dummy;
    int fd;
    int major, minor, rev, ver = -1;

    if ((verstring = (char *) malloc(MAX_STR_LEN)) == NULL) {
        ALOGE("Failed to allocate memory\n");
        return -1;
    }
    if ((dummy = (char *) malloc(MAX_STR_LEN)) == NULL) {
        ALOGE("Failed to allocate memory\n");
        free(verstring);
        return -1;
    }

    if ((fd = open("/proc/version", O_RDONLY)) < 0) {
        ALOGE("Failed to open file /proc/version\n");
        goto ret;
    }

    if (read(fd, verstring, MAX_STR_LEN) < 0) {
        ALOGE("Failed to read kernel version string from /proc/version file\n");
        close(fd);
        goto ret;
    }
    close(fd);
    if (sscanf(verstring, "%s %s %d.%d.%d%s\n", dummy, dummy, &major, &minor, &rev, dummy) != 6) {
        ALOGE("Failed to read kernel version numbers\n");
        goto ret;
    }
    ver = KERNEL_VERSION(major, minor, rev);
ret:
    free(verstring);
    free(dummy);
    return ver;
}

CameraHardware::~CameraHardware()
{
    mCamera->Uninit(STATE_PREVIEW);
    mCamera->StopStreaming(STATE_PREVIEW);
    mCamera->Close();
    if (neon_args) {
        free((NEON_FUNCTION_ARGS*)neon_args);
    }
    if (pTIrtn != NULL) {
        dlclose(pTIrtn);
        pTIrtn = NULL;
        ALOGD("Unloaded NEON Rotation Library");
    }
    delete mCamera;
    mCamera = 0;

    // Re-enable ISP resizer on overlay for 720P playback
    system("echo 1 > "
            "/sys/devices/platform/dsscomp/isprsz/enable");  
}

void CameraHardware::setCallbacks(camera_notify_callback notify_cb,
                                  camera_data_callback data_cb,
                                  camera_data_timestamp_callback data_cb_timestamp,
                                  camera_request_memory get_memory,
                                  void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mRequestMemory = get_memory;
    mCallbackCookie = user;
    return;
}

status_t CameraHardware::setPreviewWindow(preview_stream_ops *w)
{
    int min_bufs;

    mNativeWindow = w;
    ALOGV("%s: mNativeWindow %p", __func__, mNativeWindow);

    if (!w) {
        ALOGE("preview window is NULL!");
        return OK;
    }

    mPreviewLock.lock();

    if (mPreviewRunning && !mPreviewStartDeferred) {
        ALOGI("stop preview (window change)");
        stopPreviewInternal();
    }

    if (w->get_min_undequeued_buffer_count(w, &min_bufs)) {
        ALOGE("%s: could not retrieve min undequeued buffer count", __func__);
        return INVALID_OPERATION;
    }

    if (min_bufs >= NB_BUFFER) {
        ALOGE("%s: min undequeued buffer count %d is too high (expecting at most %d)", __func__,
             min_bufs, NB_BUFFER - 1);
    }

    ALOGV("%s: setting buffer count to %d", __func__, NB_BUFFER);
    if (w->set_buffer_count(w, NB_BUFFER)) {
        ALOGE("%s: could not set buffer count", __func__);
        return INVALID_OPERATION;
    }

    int preview_width;
    int preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    int hal_pixel_format = HAL_PIXEL_FORMAT_YUV;

    const char *str_preview_format = mParameters.getPreviewFormat();
    ALOGV("%s: preview format %s", __func__, str_preview_format);

    if (w->set_usage(w, CAMHAL_GRALLOC_USAGE)) {
        ALOGE("%s: could not set usage on gralloc buffer", __func__);
        return INVALID_OPERATION;
    }

    if (w->set_buffers_geometry(w,
                                preview_width, preview_height,
                                hal_pixel_format)) {
        ALOGE("%s: could not set buffers geometry to %s",
             __func__, str_preview_format);
        return INVALID_OPERATION;
    }

    if (mPreviewRunning && mPreviewStartDeferred) {
        ALOGV("start/resume preview");
        status_t ret = startPreviewInternal();
        if (ret == OK) {
            mPreviewStartDeferred = false;
            mPreviewCondition.signal();
        }
    }
    mPreviewLock.unlock();

    return OK;
}

void CameraHardware::enableMsgType(int32_t msgType)
{
    ALOGV("enableMsgType:%d", msgType);
    mMsgEnabled |= msgType;
}

void CameraHardware::disableMsgType(int32_t msgType)
{
    ALOGV("disableMsgType:%d", msgType);
    mMsgEnabled &= ~msgType;
}

bool CameraHardware::msgTypeEnabled(int32_t msgType)
{
    ALOGV("msgTypeEnabled:%d", msgType);
    return (mMsgEnabled & msgType);
}

bool CameraHardware::validateSize(size_t width, size_t height, const supported_resolution *supRes, size_t count)
{
    bool ret = false;
    status_t stat = NO_ERROR;

    if (NULL == supRes) {
        ALOGE("Invalid resolutions array passed");
        stat = -EINVAL;
    }

    if (NO_ERROR == stat) {
        for (unsigned int i = 0; i < count; i++) {
            // ALOGD( "Validating %d, %d and %d, %d", supRes[i].width, width, supRes[i].height, height);
            if (( supRes[i].width == width) && (supRes[i].height == height)) {
                ret = true;
                break;
            }
        }
    }
    return ret;
}

// ---------------------------------------------------------------------------
static void showFPS(const char *tag)
{
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;

    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        ALOGD("[%s] %d Frames, %f FPS", tag, mFrameCount, mFps);
    }
}

int CameraHardware::previewThreadWrapper()
{
    ALOGI("%s: starting", __func__);
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            ALOGI("%s: calling mCamera->stopPreview() and waiting", __func__);
            mCamera->stopPreview();
            /* signal that we're stopping */
            mPreviewStoppedCondition.signal();
            mPreviewCondition.wait(mPreviewLock);
            ALOGI("%s: return from wait", __func__);
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
            ALOGI("%s: exiting", __func__);
            mCamera->stopPreview();
            return 0;
        }
        previewThread();
    }
}

int CameraHardware::previewThread()
{
    int index;
    nsecs_t timestamp;
    void *tempbuf;
    int width, height;
    int framesize, preview_framesize;
    int offset;
    int maxInterval = MAX_INTERVAL;

    mParameters.getPreviewSize(&width, &height);

    if (height > 500 && mRecordingEnabled)
        maxInterval = MAX_INTERVAL * 3 / 2;

    framesize = width * height * 2;
    preview_framesize = width * height * 3 / 2;

    if (UNLIKELY(mDebugFps)) {
        showFPS("Preview");
    }

    mRecordingLock.lock();
    if (buffersQueued > 5) {
        mRecordingLock.unlock();
        return NO_ERROR;
    }
    mRecordingLock.unlock();

    tempbuf = mCamera->GrabFrame(index);

    offset = preview_framesize * index;

    timestamp = systemTime(SYSTEM_TIME_MONOTONIC);

    if ((timestamp - mLastShownTime) > MIN_INTERVAL && (timestamp - mLastShownTime) < maxInterval) {
        ALOGV("Skipping preview frame: %lldns\n", (timestamp - mLastShownTime));
        goto callbacks;
    }
    mLastShownTime = timestamp;

    if (mNativeWindow && mGrallocHal) {
        buffer_handle_t *buf_handle;
        int stride;
        if (0 != mNativeWindow->dequeue_buffer(mNativeWindow, &buf_handle, &stride)) {
            ALOGE("Could not dequeue gralloc buffer!\n");
            goto callbacks;
        }

        void *vaddr;
        if (!mGrallocHal->lock(mGrallocHal,
                               *buf_handle,
                               CAMHAL_GRALLOC_USAGE,
                               0, 0, width, height, &vaddr)) {
            if (mCameraID == CAMERA_FF) {
                if (scale_process((void*)tempbuf, PREVIEW_WIDTH, PREVIEW_HEIGHT, (void*)mScaleHeap->data, PREVIEW_HEIGHT, PREVIEW_WIDTH, 0, PIX_YUV422I, 1)) {
                    ALOGE("scale_process() failed\n");
                }
                neon_args->pIn = (uint8_t*)mScaleHeap->data;
                neon_args->pOut = (uint8_t*)mFrameScaled->data;
                neon_args->width = PREVIEW_HEIGHT;
                neon_args->height = PREVIEW_WIDTH;
                neon_args->rotate = NEON_ROT90;
                int error = 0;
                if (Neon_Rotate != NULL)
                    error = (*Neon_Rotate)(neon_args);
                else
                    ALOGE("Rotate Fucntion pointer Null");

                if (error < 0) {
                    ALOGE("Error in Rotation 90");

                }
                memcpy((unsigned char *)vaddr, (unsigned char *)mFrameScaled->data, framesize);
            } else
                memcpy((unsigned char *)vaddr, (unsigned char *)tempbuf, framesize);

            mGrallocHal->unlock(mGrallocHal, *buf_handle);
        } else
            ALOGE("%s: could not obtain gralloc buffer", __func__);

        if (0 != mNativeWindow->enqueue_buffer(mNativeWindow, buf_handle)) {
            ALOGE("Could not enqueue gralloc buffer!\n");
            goto callbacks;
        }
    }

callbacks:
    // Notify the client of a new frame.
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        const char * preview_format = mParameters.getPreviewFormat();
        if (!strcmp(preview_format, CameraParameters::PIXEL_FORMAT_YUV420SP)) {
            if (mCameraID == CAMERA_FF)
                Neon_Convert_yuv422_to_NV21((unsigned char*)mFrameScaled->data, (unsigned char*)mPreviewHeap->data + offset, width, height);
            else
                Neon_Convert_yuv422_to_NV21((unsigned char*)tempbuf, (unsigned char*)mPreviewHeap->data + offset, width, height);
        }
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap, index, NULL, mCallbackCookie);
    }

    if (mRecordingEnabled == true) {
        if (mCameraID == CAMERA_FF)
            memcpy(mRecordHeap[index]->data, mFrameScaled->data, framesize);
        else
            memcpy(mRecordHeap[index]->data, tempbuf, framesize);
        mRecordingLock.lock();
        buffersQueued++;
        mRecordBufferState[index] = 1;
        mRecordingLock.unlock();

        // Notify the client of a new frame.
        if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap[index], 0, mCallbackCookie);
        }
    } else {
        mCamera->ReleaseFrame(index);
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview()
{
    int ret = 0;

    mPreviewLock.lock();
    if (mPreviewRunning) {
        // already running
        ALOGE("%s : preview thread already running", __func__);
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }

    mPreviewRunning = true;
    mPreviewStartDeferred = false;

    if (!mNativeWindow) {
        ALOGI("%s : deferring", __func__);
        mPreviewStartDeferred = true;
        mPreviewLock.unlock();
        return NO_ERROR;
    }

    ret = startPreviewInternal();
    if (ret == OK)
        mPreviewCondition.signal();

    mPreviewLock.unlock();
    return ret;
}

status_t CameraHardware::startPreviewInternal()
{
    int ret = 0;
    int fps = mParameters.getPreviewFrameRate();
    int framesize;
    int preview_framesize;

    mParameters.getPreviewSize(&mPreviewWidth, &mPreviewHeight);
    ALOGD("startPreview width:%d,height:%d", mPreviewWidth, mPreviewHeight);
    if (mPreviewWidth <= 0 || mPreviewHeight <= 0) {
        ALOGE("Preview size is not valid,aborting..Device can not open!!!");
        return INVALID_OPERATION;
    }

    framesize = mPreviewWidth * mPreviewHeight * 2;
    preview_framesize = mPreviewWidth * mPreviewHeight * 3 / 2;

    if (mPreviewHeap) {
        mPreviewHeap->release(mPreviewHeap);
        mPreviewHeap = NULL;
    }
    mPreviewHeap = mRequestMemory(-1, preview_framesize, NB_BUFFER, NULL);

    for (int i = 0; i < NB_BUFFER; i++) {
        if (mRecordHeap[i] != NULL) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = 0;
        }
        mRecordBufferState[i] = 0;
        mRecordHeap[i] = mRequestMemory(-1, framesize, 1, NULL);
    }

    if (mCameraID == CAMERA_FF) {
        fps = 15;

        if (mScaleHeap) {
            mScaleHeap->release(mScaleHeap);
            mScaleHeap = NULL;
        }
        mScaleHeap = mRequestMemory(-1, framesize, 1, NULL);

        if (mFrameScaled) {
            mFrameScaled->release(mFrameScaled);
            mFrameScaled = NULL;
        }
        mFrameScaled = mRequestMemory(-1, framesize, 1, NULL);
    }

    ret = mCamera->Configure(mPreviewWidth, mPreviewHeight, PIXEL_FORMAT, fps, STATE_PREVIEW);
    if (ret < 0) {
        ALOGE("Fail to configure camera device");
        return INVALID_OPERATION;
    }

    ret = mCamera->BufferMap(STATE_PREVIEW);
    if (ret) {
        ALOGE("Camera Init fail: %s", strerror(errno));
        return UNKNOWN_ERROR;
    }

    ret = mCamera->StartStreaming(0);
    if (ret) {
        ALOGE("Camera StartStreaming fail: %s", strerror(errno));
        mCamera->Uninit(STATE_PREVIEW);
        mCamera->Close();
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

void CameraHardware::stopPreviewInternal()
{
    ALOGV("%s :", __func__);

    /* request that the preview thread stop. */
    if (mPreviewRunning) {
        mPreviewRunning = false;
        if (!mPreviewStartDeferred) {
            mPreviewCondition.signal();
            /* wait until preview thread is stopped */
            mPreviewStoppedCondition.wait(mPreviewLock);
        } else
            ALOGV("%s : preview running but deferred, doing nothing", __func__);
    } else
        ALOGI("%s : preview not running, doing nothing", __func__);
}

void CameraHardware::stopPreview()
{
    mPreviewLock.lock();
    stopPreviewInternal();
    mPreviewLock.unlock();
    return;
}

bool CameraHardware::previewEnabled()
{
    Mutex::Autolock lock(mPreviewLock);
    ALOGV("%s : %d", __func__, mPreviewRunning);
    return mPreviewRunning;
}

status_t CameraHardware::startRecording()
{
    ALOGE("startRecording");

    // Boost DSP OPP to highest level
    SetDSPKHz(DSP3630_KHZ_MAX);

    mRecordingLock.lock();
    buffersQueued = 0;

    mRecordingEnabled = true;
    mRecordingLock.unlock();
    return NO_ERROR;

}

void CameraHardware::stopRecording()
{
    ALOGE("stopRecording");
    mRecordingLock.lock();
    if (mRecordingEnabled) {
        mRecordingEnabled = false;
        for (int i = 0; i < NB_BUFFER; i++) {
            if (mRecordBufferState[i] != 0) {
                mCamera->ReleaseFrame(i);
                mRecordBufferState[i] = 0;
            }
        }
    }
    mRecordingLock.unlock();

    // Release constraint to DSP OPP by setting lowest Hz
    SetDSPKHz(DSP3630_KHZ_MIN);
}

bool CameraHardware::recordingEnabled()
{
    return mRecordingEnabled;
}

void CameraHardware::releaseRecordingFrame(const void* opaque)
{
    int i;
    mRecordingLock.lock();
    for (i = 0; i < NB_BUFFER; i++) {
        if ((void*)opaque == (void*)mRecordHeap[i]->data && mRecordBufferState[i] == 1) {
            mCamera->ReleaseFrame(i);
            mRecordBufferState[i] = 0;
            buffersQueued--;
            break;
        }
    }
    mRecordingLock.unlock();
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware*)cookie;
    ALOGV("beginAutoFocusThread");
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    int af_status = 0;

    ALOGV("%s : starting", __func__);

    /* block until we're told to start. we don't want to use
     * a restartable thread and requestExitAndWait() in cancelAutoFocus()
     * because it would cause deadlock between our callbacks and the
     * caller of cancelAutoFocus() which both want to grab the same lock
     * in CameraServices layer.
     */

    mFocusLock.lock();
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request0", __func__);
        return NO_ERROR;
    }
    mFocusCondition.wait(mFocusLock);
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request1", __func__);
        return NO_ERROR;
    }
    mFocusLock.unlock();

    //dhiru1602 :  Release focus lock when autofocus is called again.
    mCamera->setAEAWBLockUnlock(0, 0);

    ALOGV("%s : calling setAutoFocus", __func__);
    if (mCamera->setAutofocus() < 0) {
        ALOGE("ERR(%s):Fail on mSecCamera->setAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }

    af_status = mCamera->getAutoFocusResult();

    if (af_status == 0x01) {
        ALOGV("%s : AF Success!!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
    } else if (af_status == 0x02) {
        ALOGV("%s : AF Cancelled !!", __func__);
        if (mMsgEnabled & CAMERA_MSG_FOCUS) {
            /* CAMERA_MSG_FOCUS only takes a bool. true for
             * finished and false for failure. cancel is still
             * considered a true result.
             */
            mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
        }
    } else {
        ALOGV("%s : AF Fail !!", __func__);
        ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
    }

    ALOGV("%s : exiting with no error", __func__);
    return NO_ERROR;
}

status_t CameraHardware::autoFocus()
{

    mFocusCondition.signal();
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{

    if (mPreviewThread == NULL || mCameraID == CAMERA_FF)
        return NO_ERROR;

    if (mCamera->cancelAutofocus() < 0) {
        ALOGE("ERR(%s):Fail on mCamera->cancelAutofocus()", __func__);
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware*)cookie;
    ALOGE("begin Picture Thread");
    return c->pictureThread();
}

int CameraHardware::pictureThread()
{
    unsigned char *frame;
    int w, h;
    int ret;
    camera_memory_t* picture;

    ALOGV("Picture Thread:%d", mMsgEnabled);
    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    mParameters.getPictureSize(&w, &h);
    ALOGV("Picture Size: Width = %d \t Height = %d", w, h);

    int width, height;
    mParameters.getPictureSize(&width, &height);
    int fps = mParameters.getPreviewFrameRate();
    int pixelformat = V4L2_PIX_FMT_JPEG;

    if (mCameraID == CAMERA_FF) {
        fps = 15;
        pixelformat = PIXEL_FORMAT;
    }
    ret = mCamera->Configure(width, height, pixelformat, fps, STATE_PICTURE);
    if (ret < 0) {
        ALOGE("Fail to configure camera device");
        return INVALID_OPERATION;
    }
    ret = mCamera->BufferMap(STATE_PICTURE);
    if (ret) {
        ALOGE("Camera BufferMap fail: %s", strerror(errno));
        return UNKNOWN_ERROR;
    }

    ret = mCamera->StartStreaming(1);
    if (ret) {
        ALOGE("Camera StartStreaming fail: %s", strerror(errno));
        mCamera->Uninit(STATE_PICTURE);
        mCamera->Close();
        return UNKNOWN_ERROR;
    }

    camera_memory_t* tempbuf = NULL;

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        mCamera->GrabRawFrame(tempbuf, w, h);
        mDataCb(CAMERA_MSG_RAW_IMAGE, tempbuf, 0, NULL, mCallbackCookie);
    } else if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY) {
        mNotifyCb(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0, 0, mCallbackCookie);
    }

    //TODO xxx : Optimize the memory capture call. Too many memcpy
    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        ALOGV("mJpegPictureCallback");
        void* mRawBuffer;
        int JpegImageSize, JpegExifSize, JpegThumbnailSize, JpegThumbnailOffset;

        int framesize = w * h * 2;
        camera_memory_t *ExifHeap = mRequestMemory(-1, EXIF_FILE_SIZE, 1, 0);
        camera_memory_t *mem;

        if (mCameraID == CAMERA_FF) {
            camera_memory_t *mScaleHeap = mRequestMemory(-1, framesize, 1, NULL);
            camera_memory_t *mRotateHeap = mRequestMemory(-1, framesize, 1, NULL);

            mRawBuffer = mCamera->GrabJpegFrame(JpegImageSize, true);
            if (scale_process(mRawBuffer, w, h, (void*)mScaleHeap->data, h, w, 0, PIX_YUV422I, 1)) {
                ALOGE("scale_process() failed\n");
            }
            neon_args->pIn = (uint8_t*)mScaleHeap->data;
            neon_args->pOut = (uint8_t*)mRotateHeap->data;
            neon_args->width = h;
            neon_args->height = w;
            neon_args->rotate = NEON_ROT90;
            int error = 0;
            if (Neon_Rotate != NULL)
                error = (*Neon_Rotate)(neon_args);
            else
                ALOGE("Rotate Fucntion pointer Null");

            if (error < 0) {
                ALOGE("Error in Rotation 90");
            }

            CreateExif(NULL, 0, (unsigned char*)ExifHeap->data, JpegExifSize, 0);

            picture = mRequestMemory(-1, framesize, 1, NULL);
            JpegImageSize = encodeImage(picture->data,                          //Output Buffer
                                        mRotateHeap->data,                      // Input Buffer
                                        w,                                      //Image Width
                                        h,                                      //Image Height
                                        100,                                    //Quality
                                        (unsigned char*)ExifHeap->data,         //JPEG Exif Buffer
                                        JpegExifSize,                           //JPEG Exif Size
                                        mThumbnailWidth,                        //Thumbnail Width
                                        mThumbnailHeight);                      //Thumbnail Height
            mScaleHeap->release(mScaleHeap);
            mRotateHeap->release(mRotateHeap);

            mem = mRequestMemory(-1, JpegImageSize, 1, 0);
            memcpy(mem->data, picture->data, JpegImageSize);
        } else {
            mRawBuffer = mCamera->GrabJpegFrame(JpegImageSize, false);
            picture = mRequestMemory(-1, sizeof(unsigned char*), 1, NULL);
            picture->data = (unsigned char*)mRawBuffer;
            JpegThumbnailSize = mCamera->GetThumbNailDataSize();
            JpegThumbnailOffset = mCamera->GetThumbNailOffset();
            CreateExif((unsigned char*)((uint8_t*)mRawBuffer + JpegThumbnailOffset), JpegThumbnailSize, (unsigned char*)ExifHeap->data, JpegExifSize, 1);

            mem = mRequestMemory(-1, JpegImageSize + JpegExifSize, 1, 0);
            uint8_t *ptr = (uint8_t*)mem->data;
            memcpy(ptr, picture->data, 2); ptr += 2;
            memcpy(ptr, ExifHeap->data, JpegExifSize); ptr += JpegExifSize;
            memcpy(ptr, (uint8_t*)picture->data + 2, JpegImageSize - 2);
        }

        ALOGD("JpegExifSize=%d, JpegImageSize=%d", JpegExifSize, JpegImageSize);

        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, 0, NULL, mCallbackCookie);

        mem->release(mem);
        ExifHeap->release(ExifHeap);
        picture->release(picture);
    }

    /* Close operation */
    mCamera->Uninit(STATE_PICTURE);
    mCamera->StopStreaming(STATE_PICTURE);

    ALOGV("End pictureThread()");
    return NO_ERROR;
}

status_t CameraHardware::takePicture()
{
    ALOGV("pictureThread()");
    stopPreview();
    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) != NO_ERROR) {
        ALOGE("%s : couldn't run picture thread", __func__);
        return INVALID_OPERATION;
    }
    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{
    if (mPictureThread.get()) {
        ALOGV("%s: waiting for picture thread to exit", __func__);
        mPictureThread->requestExitAndWait();
        ALOGV("%s: picture thread has exited", __func__);
    }
    return NO_ERROR;
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    return NO_ERROR;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    int width  = 0;
    int height = 0;
    int framerate = 0;
    int ret;

    params.getPreviewSize(&width, &height);

    ALOGV("Set Parameter...!! ");

    ALOGV("PreviewFormat %s", params.getPreviewFormat());
    if (params.getPreviewFormat() != NULL) {
        if (strcmp(params.getPreviewFormat(), (const char*)CameraParameters::PIXEL_FORMAT_YUV420SP) != 0) {
            ALOGE("Only YUV420SP preview is supported");
            return -EINVAL;
        }
    }

    ALOGV("PictureFormat %s", params.getPictureFormat());
    if (params.getPictureFormat() != NULL) {
        if (strcmp(params.getPictureFormat(), (const char*)CameraParameters::PIXEL_FORMAT_JPEG) != 0) {
            ALOGE("Only jpeg still pictures are supported");
            return -EINVAL;
        }
    }

    framerate = params.getPreviewFrameRate();
    ALOGV("FRAMERATE %d", framerate);

    mParameters = params;

    mParameters.getPictureSize(&width, &height);
    ALOGV("Picture Size by CamHAL %d x %d", width, height);

    mParameters.getPreviewSize(&width, &height);
    ALOGV("Preview Resolution by CamHAL %d x %d", width, height);
    mParameters.setPreviewSize(width, height);

    // whitebalance
    const char *new_white_str = params.get(CameraParameters::KEY_WHITE_BALANCE);
    ALOGV("%s : new_white_str %s", __func__, new_white_str);
    if (new_white_str != NULL) {
        int new_white = -1;

        if (!strcmp(new_white_str, (const char*)CameraParameters::WHITE_BALANCE_AUTO))
            new_white = WHITE_BALANCE_AUTO;
        else if (!strcmp(new_white_str,
                         (const char*)CameraParameters::WHITE_BALANCE_DAYLIGHT))
            new_white = WHITE_BALANCE_SUNNY;
        else if (!strcmp(new_white_str,
                         (const char*)CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
            new_white = WHITE_BALANCE_CLOUDY;
        else if (!strcmp(new_white_str,
                         (const char*)CameraParameters::WHITE_BALANCE_FLUORESCENT))
            new_white = WHITE_BALANCE_FLUORESCENT;
        else if (!strcmp(new_white_str,
                         (const char*)CameraParameters::WHITE_BALANCE_INCANDESCENT))
            new_white = WHITE_BALANCE_TUNGSTEN;
        else {
            ALOGE("ERR(%s):Invalid white balance(%s)", __func__, new_white_str); //twilight, shade, warm_flourescent
        }

        if (0 <= new_white) {
            if (mCamera->setWhiteBalance(new_white) < 0) {
                ALOGE("ERR(%s):Fail on mCamera->setWhiteBalance(white(%d))", __func__, new_white);
            } else {
                mParameters.set(CameraParameters::KEY_WHITE_BALANCE, new_white_str);
            }
        }
    }

    // brightness
    int new_exposure_compensation = params.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    int max_exposure_compensation = params.getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
    int min_exposure_compensation = params.getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
    ALOGV("%s : new_exposure_compensation %d", __func__, new_exposure_compensation);
    if ((min_exposure_compensation <= new_exposure_compensation) &&
        (max_exposure_compensation >= new_exposure_compensation)) {
        if (mCamera->setBrightness(new_exposure_compensation) < 0) {
            ALOGE("ERR(%s):Fail on mCamera->setBrightness(brightness(%d))", __func__, new_exposure_compensation);
        } else {
            mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, new_exposure_compensation);
        }
    }

    // scene mode
    const char *new_scene_mode_str = params.get(CameraParameters::KEY_SCENE_MODE);
    const char *current_scene_mode_str = mParameters.get(CameraParameters::KEY_SCENE_MODE);

    // Recording Hint
    const char *recordinghint = mParameters.get(CameraParameters::KEY_RECORDING_HINT);
    if (!recordinghint || (recordinghint && (strcmp(recordinghint, CameraParameters::FALSE)))) {
        mCamera->setCamMode(MODE_CAMCORDER);
    } else {
        mCamera->setCamMode(MODE_CAMERA);
    }

    // fps range
    int new_min_fps = 0;
    int new_max_fps = 0;
    int current_min_fps, current_max_fps;
    params.getPreviewFpsRange(&new_min_fps, &new_max_fps);
    mParameters.getPreviewFpsRange(&current_min_fps, &current_max_fps);
    /* our fps range is determined by the sensor, reject any request
     * that isn't exactly what we're already at.
     * but the check is performed when requesting only changing fps range
     */
    if (new_scene_mode_str && current_scene_mode_str) {
        if (!strcmp(new_scene_mode_str, current_scene_mode_str)) {
            if ((new_min_fps != current_min_fps) || (new_max_fps != current_max_fps)) {
                ALOGW("%s : requested new_min_fps = %d, new_max_fps = %d not allowed",
                      __func__, new_min_fps, new_max_fps);
                ALOGE("%s : current_min_fps = %d, current_max_fps = %d",
                      __func__, current_min_fps, current_max_fps);
                ret = UNKNOWN_ERROR;
            }
        }
    } else {
        /* Check basic validation if scene mode is different */
        if ((new_min_fps > new_max_fps) ||
            (new_min_fps < 0) || (new_max_fps < 0))
            ret = UNKNOWN_ERROR;
    }

    // JPEG thumbnail size
    int new_jpeg_thumbnail_width = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int new_jpeg_thumbnail_height = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    if (mThumbnailWidth != new_jpeg_thumbnail_width || mThumbnailHeight <= new_jpeg_thumbnail_height) {
        mThumbnailWidth = new_jpeg_thumbnail_width;
        mThumbnailHeight = new_jpeg_thumbnail_height;
    } else {
        mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, mThumbnailWidth);
        mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, mThumbnailHeight);
    }

    const char *new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);

    if (mCameraID == CAMERA_BF) {
        int new_scene_mode = -1;

        // fps range is (15000,30000) by default.
        mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
        mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,
                        "15000,30000");

        if (!strcmp(new_scene_mode_str, (const char*)CameraParameters::SCENE_MODE_AUTO)) {
            new_scene_mode = SCENE_MODE_OFF;
        } else {
            // defaults for non-auto scene modes
            if (mCameraID == CAMERA_BF) {
                new_focus_mode_str = CameraParameters::FOCUS_MODE_AUTO;
            }

            if (!strcmp(new_scene_mode_str,
                        (const char*)CameraParameters::SCENE_MODE_PORTRAIT)) {
                new_scene_mode = SCENE_MODE_PORTRAIT;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_LANDSCAPE)) {
                new_scene_mode = SCENE_MODE_LANDSCAPE;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_NIGHT)) {
                new_scene_mode = SCENE_MODE_NIGHTSHOT;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_BEACH)) {
                new_scene_mode = SCENE_MODE_BEACH_SNOW;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_SNOW)) {
                new_scene_mode = SCENE_MODE_BEACH_SNOW;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_SUNSET)) {
                new_scene_mode = SCENE_MODE_SUNSET;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_FIREWORKS)) {
                new_scene_mode = SCENE_MODE_FIREWORKS;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_SPORTS)) {
                new_scene_mode = SCENE_MODE_SPORTS;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_CANDLELIGHT)) {
                new_scene_mode = SCENE_MODE_CANDLELIGHT;
            } else if (!strcmp(new_scene_mode_str,
                               (const char*)CameraParameters::SCENE_MODE_PARTY)) {
                new_scene_mode = SCENE_MODE_INDOORS;
            } else if (!strcmp(new_scene_mode_str, "backlight")) {
                new_scene_mode = SCENE_MODE_AGAINST_LIGHT;
            } else if (!strcmp(new_scene_mode_str, "dusk-dawn")) {
                new_scene_mode = SCENE_MODE_DAWN;
            } else if (!strcmp(new_scene_mode_str, "fall-color")) {
                new_scene_mode = SCENE_MODE_FALLCOLOR;
            } else if (!strcmp(new_scene_mode_str, "text")) {
                new_scene_mode = SCENE_MODE_TEXT;
            } else {
                ALOGE("%s::unmatched scene_mode(%s)",
                      __func__, new_scene_mode_str);
                ret = UNKNOWN_ERROR;
            }
        }

        // focus mode
        if (new_focus_mode_str != NULL) {
            int new_focus_mode = -1;

            if (!strcmp(new_focus_mode_str,
                        (const char*)CameraParameters::FOCUS_MODE_AUTO)) {
                new_focus_mode = FOCUS_MODE_AUTO;
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
            } else if (!strcmp(new_focus_mode_str,
                              (const char*)CameraParameters::FOCUS_MODE_MACRO)) {
                new_focus_mode = FOCUS_MODE_MACRO;
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR);
            } else if (!strcmp(new_focus_mode_str, FOCUS_MODE_FACEDETECTION)) {
                new_focus_mode = FOCUS_MODE_FACEDETECT;
                mParameters.set(CameraParameters::KEY_FOCUS_DISTANCES,
                                BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR);
            } else {
                ALOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str);
                ret = UNKNOWN_ERROR;
            }

            if (0 <= new_focus_mode) {
                if (mCamera->setFocusMode(new_focus_mode) < 0) {
                    ALOGE("%s::mCamera->setFocusMode(%d) fail", __func__, new_focus_mode);
                    ret = UNKNOWN_ERROR;
                } else {
                    mParameters.set(CameraParameters::KEY_FOCUS_MODE, new_focus_mode_str);
                }
            }
        }

        //  scene..
        if (0 <= new_scene_mode) {
            if (mCamera->setSceneMode(new_scene_mode) < 0) {
                ALOGE("%s::mCamera->setSceneMode(%d) fail", __func__, new_scene_mode);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(CameraParameters::KEY_SCENE_MODE, new_scene_mode_str);
            }
        }

        // touch to focus
        const char *new_focus_area = params.get(CameraParameters::KEY_FOCUS_AREAS);
        if (new_focus_area != NULL) {
            ALOGV("focus area: %s", new_focus_area);
            CameraArea area(new_focus_area);

            if (!area.isDummy()) {
                mParameters.getPreviewSize(&width, &height);

                int x = area.getX(width);
                int y = area.getY(height);

                ALOGV("area=%s, x=%i, y=%i", area.toString8().string(), x, y);
                if (mCamera->setObjectPosition(x, y) < 0) {
                    ALOGE("ERR(%s):Fail on mCamera->setObjectPosition(%s)", __func__, new_focus_area);
                    ret = UNKNOWN_ERROR;
                }
            }

            int val = area.isDummy() ? 0 : 1;
            if (mCamera->setTouchAFStartStop(val) < 0) {
                ALOGE("ERR(%s):Fail on mCamera->setTouchAFStartStop(%d)", __func__, val);
                ret = UNKNOWN_ERROR;
            }
        }

        //Camera Zoom Control
        int new_zoom = params.getInt(CameraParameters::KEY_ZOOM);
        int max_zoom = params.getInt(CameraParameters::KEY_MAX_ZOOM);
        ALOGV("%s : new_zoom %d", __func__, new_zoom);
        if (0 <= new_zoom && new_zoom <= max_zoom) {
            ALOGV("%s : set zoom:%d\n", __func__, new_zoom);
            if (mCamera->setZoom(new_zoom) < 0) {
                ALOGE("ERR(%s):Fail on Camera->setZoom(%d)", __func__, new_zoom);
            } else {
                mParameters.set(CameraParameters::KEY_ZOOM, new_zoom);
            }
        }

        // ISO
        const char *new_iso_str = params.get("iso");
        ALOGV("%s : new_iso_str %s", __func__, new_iso_str);
        if (new_iso_str != NULL) {
            int new_iso = -1;
            if (!strcmp(new_iso_str, "auto")) {
                new_iso = ISO_AUTO;
            } else if (!strcmp(new_iso_str, "ISO50")) {
                new_iso = ISO_50;
            } else if (!strcmp(new_iso_str, "ISO100")) {
                new_iso = ISO_100;
            } else if (!strcmp(new_iso_str, "ISO200")) {
                new_iso = ISO_200;
            } else if (!strcmp(new_iso_str, "ISO400")) {
                new_iso = ISO_400;
            } else if (!strcmp(new_iso_str, "ISO800")) {
               new_iso = ISO_800;
            } else if (!strcmp(new_iso_str, "ISO1600")) {
               new_iso = ISO_1600;
            } else {
                ALOGE("ERR(%s):Invalid iso value(%s)", __func__, new_iso_str);
                ret = UNKNOWN_ERROR;
            }

            if (0 <= new_iso) {
                if (mCamera->setISO(new_iso) < 0) {
                    ALOGE("ERR(%s):Fail on mCamera->setISO(new_iso(%d))", __func__, new_iso);
                    ret = UNKNOWN_ERROR;
                } else {
                    mParameters.set("iso", new_iso_str);
                }
            }
        }

        // image effect
        const char *new_image_effect_str = params.get(CameraParameters::KEY_EFFECT);
        if (new_image_effect_str != NULL) {

            int new_image_effect = -1;

            if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NONE))
                new_image_effect = CAMERA_EFFECT_NONE;
            else if (!strcmp(new_image_effect_str, "sharpen"))
                new_image_effect = CAMERA_EFFECT_SHARPEN;
            else if (!strcmp(new_image_effect_str, "purple"))
                new_image_effect = CAMERA_EFFECT_PURPLE;
            else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_NEGATIVE))
                new_image_effect = CAMERA_EFFECT_NEGATIVE;
            else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_SEPIA))
                new_image_effect = CAMERA_EFFECT_SEPIA;
            else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_AQUA))
                new_image_effect = CAMERA_EFFECT_AQUA;
            else if (!strcmp(new_image_effect_str, "green-tint"))
                new_image_effect = CAMERA_EFFECT_GREEN;
            else if (!strcmp(new_image_effect_str, "blue-tint"))
                new_image_effect = CAMERA_EFFECT_BLUE;
            else if (!strcmp(new_image_effect_str, "pink"))
                new_image_effect = CAMERA_EFFECT_PINK;
            else if (!strcmp(new_image_effect_str, "yellow"))
                new_image_effect = CAMERA_EFFECT_YELLOW;
            else if (!strcmp(new_image_effect_str, "red-tint"))
                new_image_effect = CAMERA_EFFECT_RED;
            else if (!strcmp(new_image_effect_str, "antique"))
                new_image_effect = CAMERA_EFFECT_ANTIQUE;
            else if (!strcmp(new_image_effect_str, CameraParameters::EFFECT_MONO))
                new_image_effect = CAMERA_EFFECT_BW;
            else {
                //posterize, whiteboard, blackboard, solarize
                ALOGE("ERR(%s):Invalid effect(%s)", __func__, new_image_effect_str);
                ret = UNKNOWN_ERROR;
            }

            if (new_image_effect >= 0) {
                if (mCamera->setImageEffect(new_image_effect) < 0) {
                    ALOGE("ERR(%s):Fail on mCamera->setImageEffect(effect(%d))", __func__, new_image_effect);
                    ret = UNKNOWN_ERROR;
                } else {
                    mParameters.set(CameraParameters::KEY_EFFECT, new_image_effect_str);
                }
            }
        }

        //JPEG image quality
        int new_jpeg_quality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
        ALOGV("%s : new_jpeg_quality %d", __func__, new_jpeg_quality);
        /* we ignore bad values */
        if (new_jpeg_quality >=1 && new_jpeg_quality <= 100) {
            if (new_jpeg_quality > 90)
                  new_jpeg_quality = JPEG_QUALITY_SUPERFINE;
            else if (new_jpeg_quality > 75)
                  new_jpeg_quality = JPEG_QUALITY_FINE;
            else if (new_jpeg_quality > 50)
                  new_jpeg_quality = JPEG_QUALITY_NORMAL;
            else
                  new_jpeg_quality = JPEG_QUALITY_ECONOMY;

            if (mCamera->setJpegQuality(new_jpeg_quality) < 0) {
                ALOGE("ERR(%s):Fail on mCamera->setJpegQuality(quality(%d))", __func__, new_jpeg_quality);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(CameraParameters::KEY_JPEG_QUALITY, new_jpeg_quality);
            }
        }
    }
    // rotation
    int new_rotation = params.getInt(CameraParameters::KEY_ROTATION);
    if (0 <= new_rotation) {
        ALOGV("%s : set orientation:%d\n", __func__, new_rotation);
        if (mCamera->setExifOrientationInfo(new_rotation) < 0) {
            ALOGE("ERR(%s):Fail on mCamera->setExifOrientationInfo(%d)", __func__, new_rotation);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(CameraParameters::KEY_ROTATION, new_rotation);
        }
    }

    return NO_ERROR;
}

CameraParameters CameraHardware::getParameters() const
{
    CameraParameters params;

    {
        params = mParameters;
    }

    return params;
}

status_t CameraHardware::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardware::release()
{
    if (mPreviewThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable or running.  signal it so it wakes
         * up and can exit.
         */
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
    }
    if (mAutoFocusThread != NULL) {
        mFocusLock.lock();
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mFocusCondition.signal();
        mFocusLock.unlock();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
    }
    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }
    for (int i = 0; i < NB_BUFFER; i++) {
        if (mRecordHeap[i] != NULL) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = NULL;
        }
    }
    if (mPreviewHeap) {
        mPreviewHeap->release(mPreviewHeap);
        mPreviewHeap = NULL;
    }
    if (mScaleHeap) {
        mScaleHeap->release(mScaleHeap);
        mScaleHeap = NULL;
    }
    if (mFrameScaled) {
        mFrameScaled->release(mFrameScaled);
        mFrameScaled = NULL;
    }
    mCamera->Uninit(STATE_PREVIEW);
    if ((mCameraID == CAMERA_FF) && (isStart_scaler))
        scale_deinit();
    mCamera->Close();
}

void CameraHardware::CreateExif(unsigned char* pInThumbnailData, int Inthumbsize, unsigned char* pOutExifBuf, int& OutExifSize, int flag)
{
    int w = 0, h = 0;

    int orientationValue = mCamera->getOrientation();

    ExifCreator* mExifCreator = new ExifCreator();
    unsigned int ExifSize = 0;
    ExifInfoStructure ExifInfo;
    unsigned short tempISO = 0;
    struct v4l2_exif exifobj;

    // To read values from driver
    if (mCameraID == CAMERA_BF) {
        mCamera->getExifInfoFromDriver(&exifobj);
    }

    memset(&ExifInfo, 0, sizeof(ExifInfoStructure));

    strcpy( (char*)&ExifInfo.maker, "SAMSUNG");
    strcpy( (char*)&ExifInfo.model, "GT-I9003");

    mParameters.getPreviewSize(&w, &h);

    mParameters.getPictureSize((int*)&ExifInfo.imageWidth, (int*)&ExifInfo.imageHeight);
    mParameters.getPictureSize((int*)&ExifInfo.pixelXDimension, (int*)&ExifInfo.pixelYDimension);

    struct tm *t = NULL;
    time_t nTime;
    nTime = atol(mParameters.get(mParameters.KEY_GPS_TIMESTAMP));
    t = localtime(&nTime);
    m_gpsHour = t->tm_hour;
    m_gpsMin  = t->tm_min;
    m_gpsSec  = t->tm_sec;
    strftime((char *)m_gps_date, 11, "%Y-%m-%d", t);

    if (t != NULL) {
        sprintf((char*)&ExifInfo.dateTimeOriginal, "%4d:%02d:%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        sprintf((char*)&ExifInfo.dateTimeDigitized, "%4d:%02d:%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        sprintf((char*)&ExifInfo.dateTime, "%4d:%02d:%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    }

    if (mCameraID == CAMERA_BF) {
        int cam_ver = mCamera->GetCamera_version();

        ExifInfo.Camversion[0] = (cam_ver & 0xFF);
        ExifInfo.Camversion[1] = ((cam_ver >> 8) & 0xFF);
        ExifInfo.Camversion[2] = ((cam_ver >> 16) & 0xFF);
        ExifInfo.Camversion[3] = ((cam_ver >> 24) & 0xFF);

        sprintf((char*)&ExifInfo.software, "fw %02d.%02d prm %02d.%02d", ExifInfo.Camversion[2], ExifInfo.Camversion[3], ExifInfo.Camversion[0], ExifInfo.Camversion[1]);
        if (mThumbnailWidth > 0 && mThumbnailHeight > 0) {
            ExifInfo.hasThumbnail = true;
            ExifInfo.thumbStream                    = pInThumbnailData;
            ExifInfo.thumbSize                      = Inthumbsize;
            ExifInfo.thumbImageWidth                = mThumbnailWidth;
            ExifInfo.thumbImageHeight               = mThumbnailHeight;
        } else {
            ExifInfo.hasThumbnail = false;
        }

        ExifInfo.exposureProgram            = 3;
        ExifInfo.exposureMode               = 0;
        ExifInfo.contrast                   = convertToExifLMH(mCamera->getContrast(), 2);
        ExifInfo.fNumber.numerator          = 26;
        ExifInfo.fNumber.denominator        = 10;
        ExifInfo.aperture.numerator         = 26;
        ExifInfo.aperture.denominator       = 10;
        ExifInfo.maxAperture.numerator      = 26;
        ExifInfo.maxAperture.denominator    = 10;
        ExifInfo.focalLength.numerator      = 3430;
        ExifInfo.focalLength.denominator    = 1000;
        //[ 2010 05 01 exif
        ExifInfo.shutterSpeed.numerator     = exifobj.TV_Value;
        ExifInfo.shutterSpeed.denominator   = 100;
        ExifInfo.exposureTime.numerator     = 1;
        ExifInfo.exposureTime.denominator   = (unsigned int)pow(2.0, ((double)exifobj.TV_Value / 100.0));
        //]
        ExifInfo.brightness.numerator       = 5;
        ExifInfo.brightness.denominator     = 9;
        ExifInfo.iso                        = 1;
        ExifInfo.flash                      = 0;                    // default value

        // Flash
        // bit 0    -whether the flash fired
        // bit 1,2 -status of returned light
        // bit 3,4 - indicating the camera's flash mode
        // bit 5    -presence of a flash function
        // bit 6    - red-eye mode

        // refer to flash_mode[] at CameraHal.cpp
        // off = 1
        // on = 2
        // auto = 3

        // Todo : Need to implement how HAL can recognize existance of flash
        //		if( ! isFlashExist)	// pseudo code
        //			ExifInfo.flash = 32;		// bit 5 - No flash function.
        //		else
        {
            ALOGD("createExif - flashmode = %d flash result = %d", mPreviousFlashMode, ExifInfo.flash);

            // bit 0
            ExifInfo.flash = ExifInfo.flash | exifobj.flash;
            // bit 3,4
            if (mPreviousFlashMode == 3)                        // Flashmode auto
                ExifInfo.flash = ExifInfo.flash | 24;
            // bit 6
            // Todo : Need to implement about red-eye
            //			if(mPreviousFlashMode == ??)	// Flashmode red-eye
            //				ExifInfo.flash = ExifInfo.flash | 64;
        }

        switch (orientationValue) {
        case 0:
            ExifInfo.orientation                = 1;
            break;
        case 90:
            ExifInfo.orientation                = 6;
            break;
        case 180:
            ExifInfo.orientation                = 3;
            break;
        case 270:
            ExifInfo.orientation                = 8;
            break;
        default:
            ExifInfo.orientation                = 1;
            break;
        }
        //[ 2010 05 01 exif
        double calIsoValue = 0;
        calIsoValue = pow(2.0, ((double)exifobj.SV_Value / 100.0)) * 3.125;
        //]
        if (calIsoValue < 8.909) {
            tempISO = 0;
        } else if (calIsoValue >= 8.909 && calIsoValue < 11.22) {
            tempISO = 10;
        } else if (calIsoValue >= 11.22 && calIsoValue < 14.14) {
            tempISO = 12;
        } else if (calIsoValue >= 14.14 && calIsoValue < 17.82) {
            tempISO = 16;
        } else if (calIsoValue >= 17.82 && calIsoValue < 22.45) {
            tempISO = 20;
        } else if (calIsoValue >= 22.45 && calIsoValue < 28.28) {
            tempISO = 25;
        } else if (calIsoValue >= 28.28 && calIsoValue < 35.64) {
            tempISO = 32;
        } else if (calIsoValue >= 35.64 && calIsoValue < 44.90) {
            tempISO = 40;
        } else if (calIsoValue >= 44.90 && calIsoValue < 56.57) {
            tempISO = 50;
        } else if (calIsoValue >= 56.57 && calIsoValue < 71.27) {
            tempISO = 64;
        } else if (calIsoValue >= 71.27 && calIsoValue < 89.09) {
            tempISO = 80;
        } else if (calIsoValue >= 89.09 && calIsoValue < 112.2) {
            tempISO = 100;
        } else if (calIsoValue >= 112.2 && calIsoValue < 141.4) {
            tempISO = 125;
        } else if (calIsoValue >= 141.4 && calIsoValue < 178.2) {
            tempISO = 160;
        } else if (calIsoValue >= 178.2 && calIsoValue < 224.5) {
            tempISO = 200;
        } else if (calIsoValue >= 224.5 && calIsoValue < 282.8) {
            tempISO = 250;
        } else if (calIsoValue >= 282.8 && calIsoValue < 356.4) {
            tempISO = 320;
        } else if (calIsoValue >= 356.4 && calIsoValue < 449.0) {
            tempISO = 400;
        } else if (calIsoValue >= 449.0 && calIsoValue < 565.7) {
            tempISO = 500;
        } else if (calIsoValue >= 565.7 && calIsoValue < 712.7) {
            tempISO = 640;
        } else if (calIsoValue >= 712.7 && calIsoValue < 890.9) {
            tempISO = 800;
        } else if (calIsoValue >= 890.9 && calIsoValue < 1122) {
            tempISO = 1000;
        } else if (calIsoValue >= 1122 && calIsoValue < 1414) {
            tempISO = 1250;
        } else if (calIsoValue >= 1414 && calIsoValue < 1782) {
            tempISO = 160;
        } else if (calIsoValue >= 1782 && calIsoValue < 2245) {
            tempISO = 2000;
        } else if (calIsoValue >= 2245 && calIsoValue < 2828) {
            tempISO = 2500;
        } else if (calIsoValue >= 2828 && calIsoValue < 3564) {
            tempISO = 3200;
        } else if (calIsoValue >= 3564 && calIsoValue < 4490) {
            tempISO = 4000;
        } else if (calIsoValue >= 4490 && calIsoValue < 5657) {
            tempISO = 5000;
        } else if (calIsoValue >= 5657 && calIsoValue < 7127) {
            tempISO = 6400;
        } else {
            tempISO = 8000;
        }
        if (mCamera->getSceneMode() <= 1) {
            ExifInfo.meteringMode               = mCamera->getMetering();
            if (mCamera->getWhiteBalance() <= 1) {
                ExifInfo.whiteBalance               = 0;
            } else {
                ExifInfo.whiteBalance               = 1;
            }
            ExifInfo.saturation                 = convertToExifLMH(mCamera->getSaturation(), 2);
            ExifInfo.sharpness                  = convertToExifLMH(mCamera->getSharpness(), 2);
            switch (mCamera->getISO()) {
            case 2:
                ExifInfo.isoSpeedRating             = 50;
                break;
            case 3:
                ExifInfo.isoSpeedRating             = 100;
                break;
            case 4:
                ExifInfo.isoSpeedRating             = 200;
                break;
            case 5:
                ExifInfo.isoSpeedRating             = 400;
                break;
            case 6:
                ExifInfo.isoSpeedRating             = 800;
                break;
            default:
                ExifInfo.isoSpeedRating             = tempISO;
                break;
            }

            switch (mCamera->getBrightness()) {
            case 0:
                ExifInfo.exposureBias.numerator = -20;
                break;
            case 1:
                ExifInfo.exposureBias.numerator = -15;
                break;
            case 2:
                ExifInfo.exposureBias.numerator = -10;
                break;
            case 3:
                ExifInfo.exposureBias.numerator =  -5;
                break;
            case 4:
                ExifInfo.exposureBias.numerator =   0;
                break;
            case 5:
                ExifInfo.exposureBias.numerator =   5;
                break;
            case 6:
                ExifInfo.exposureBias.numerator =  10;
                break;
            case 7:
                ExifInfo.exposureBias.numerator =  15;
                break;
            case 8:
                ExifInfo.exposureBias.numerator =  20;
                break;
            default:
                ExifInfo.exposureBias.numerator = 0;
                break;
            }
            ExifInfo.exposureBias.denominator       = 10;
            ExifInfo.sceneCaptureType               = 0;
        } else {
            switch (mCamera->getSceneMode()) {
            case 3:                            //sunset
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 1;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 4:                            //dawn
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 1;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 5:                            //candlelight
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 1;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 6:                            //beach & snow
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 2;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = 50;
                ExifInfo.exposureBias.numerator     = 10;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 1;
                break;
            case 7:                            //againstlight
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                if (mPreviousFlashMode <= 1) {
                    ExifInfo.meteringMode           = 3;
                } else {
                    ExifInfo.meteringMode           = 2;
                }
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 8:                            //text
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 2;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 9:                            //night
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 3;
                break;
            case 10:                            //landscape
                ExifInfo.meteringMode               = 5;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 2;
                ExifInfo.sharpness                  = 2;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 1;
                break;
            case 11:                            //fireworks
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = 50;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 12:                            //portrait
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 1;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 2;
                break;
            case 13:                            //fallcolor
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 2;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 14:                            //indoors
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 2;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = 200;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            case 15:                            //sports
                ExifInfo.meteringMode               = 2;
                ExifInfo.whiteBalance               = 0;
                ExifInfo.saturation                 = 0;
                ExifInfo.sharpness                  = 0;
                ExifInfo.isoSpeedRating             = tempISO;
                ExifInfo.exposureBias.numerator     = 0;
                ExifInfo.exposureBias.denominator   = 10;
                ExifInfo.sceneCaptureType           = 4;
                break;
            }
        }
    } else {         // VGA Camera
        if (mThumbnailWidth > 0 && mThumbnailHeight > 0) {
            //thumb nail data added here
            ExifInfo.thumbStream                = pInThumbnailData;
            ExifInfo.thumbSize                  = Inthumbsize;
            ExifInfo.thumbImageWidth            = mThumbnailWidth;
            ExifInfo.thumbImageHeight           = mThumbnailHeight;
            ExifInfo.hasThumbnail               = true;
        } else {
            ExifInfo.hasThumbnail = false;
        }
        ExifInfo.exposureProgram            = 3;
        ExifInfo.exposureMode               = 0;
        ExifInfo.contrast                   = 0;
        ExifInfo.fNumber.numerator          = 28;
        ExifInfo.fNumber.denominator        = 10;
        ExifInfo.aperture.numerator         = 26;
        ExifInfo.aperture.denominator       = 10;
        ExifInfo.maxAperture.numerator      = 26;
        ExifInfo.maxAperture.denominator    = 10;
        ExifInfo.focalLength.numerator      = 900;
        ExifInfo.focalLength.denominator    = 1000;
        ExifInfo.shutterSpeed.numerator     = 16;
        ExifInfo.shutterSpeed.denominator   = 1;
        ExifInfo.brightness.numerator       = 5;
        ExifInfo.brightness.denominator     = 9;
        ExifInfo.iso                        = 1;

        switch (orientationValue) {
        case 0:
            ExifInfo.orientation                = 1;
            break;
        case 90:
            ExifInfo.orientation                = 6;
            break;
        case 180:
            ExifInfo.orientation                = 3;
            break;
        case 270:
            ExifInfo.orientation                = 8;
            break;
        default:
            ExifInfo.orientation                = 1;
            break;
        }

        ExifInfo.meteringMode               = METERING_CENTER;
        ExifInfo.whiteBalance               = 0;
        ExifInfo.saturation                 = 0;
        ExifInfo.sharpness                  = 0;
        ExifInfo.isoSpeedRating             = 100;
        ExifInfo.exposureTime.numerator     = 1;
        ExifInfo.exposureTime.denominator   = 16;
        ExifInfo.flash                      = 0;
        switch (mCamera->getBrightness()) {
        case 0:
            ExifInfo.exposureBias.numerator = -20;
            break;
        case 1:
            ExifInfo.exposureBias.numerator = -15;
            break;
        case 2:
            ExifInfo.exposureBias.numerator = -10;
            break;
        case 3:
            ExifInfo.exposureBias.numerator =  -5;
            break;
        case 4:
            ExifInfo.exposureBias.numerator =   0;
            break;
        case 5:
            ExifInfo.exposureBias.numerator =   5;
            break;
        case 6:
            ExifInfo.exposureBias.numerator =  10;
            break;
        case 7:
            ExifInfo.exposureBias.numerator =  15;
            break;
        case 8:
            ExifInfo.exposureBias.numerator =  20;
            break;
        default:
            ExifInfo.exposureBias.numerator = 0;
            break;
        }
        ExifInfo.exposureBias.denominator       = 10;
        ExifInfo.sceneCaptureType               = 0;
    }

    double arg0, arg3;
    int arg1, arg2;

    if (mParameters.get(mParameters.KEY_GPS_LATITUDE) != 0 && mParameters.get(mParameters.KEY_GPS_LONGITUDE) != 0) {
        arg0 = getGPSLatitude();

        if (arg0 > 0)
            ExifInfo.GPSLatitudeRef[0] = 'N';
        else
            ExifInfo.GPSLatitudeRef[0] = 'S';

        convertFromDecimalToGPSFormat(fabs(arg0), arg1, arg2, arg3);

        ExifInfo.GPSLatitude[0].numerator = arg1;
        ExifInfo.GPSLatitude[0].denominator = 1;
        ExifInfo.GPSLatitude[1].numerator = arg2;
        ExifInfo.GPSLatitude[1].denominator = 1;
        ExifInfo.GPSLatitude[2].numerator = arg3;
        ExifInfo.GPSLatitude[2].denominator = 1;

        arg0 = getGPSLongitude();

        if (arg0 > 0)
            ExifInfo.GPSLongitudeRef[0] = 'E';
        else
            ExifInfo.GPSLongitudeRef[0] = 'W';

        convertFromDecimalToGPSFormat(fabs(arg0), arg1, arg2, arg3);

        ExifInfo.GPSLongitude[0].numerator = arg1;
        ExifInfo.GPSLongitude[0].denominator = 1;
        ExifInfo.GPSLongitude[1].numerator = arg2;
        ExifInfo.GPSLongitude[1].denominator = 1;
        ExifInfo.GPSLongitude[2].numerator = arg3;
        ExifInfo.GPSLongitude[2].denominator = 1;

        arg0 = getGPSAltitude();

        if (arg0 > 0)
            ExifInfo.GPSAltitudeRef = 0;
        else
            ExifInfo.GPSAltitudeRef = 1;

        ExifInfo.GPSAltitude[0].numerator = fabs(arg0);
        ExifInfo.GPSAltitude[0].denominator = 1;

        //GPS_Time_Stamp
        ExifInfo.GPSTimestamp[0].numerator = (uint32_t)m_gpsHour;
        ExifInfo.GPSTimestamp[0].denominator = 1;
        ExifInfo.GPSTimestamp[1].numerator = (uint32_t)m_gpsMin;
        ExifInfo.GPSTimestamp[1].denominator = 1;
        ExifInfo.GPSTimestamp[2].numerator = (uint32_t)m_gpsSec;
        ExifInfo.GPSTimestamp[2].denominator = 1;

        //GPS_ProcessingMethod
        strcpy((char*)ExifInfo.GPSProcessingMethod, mParameters.get(mParameters.KEY_GPS_PROCESSING_METHOD));

        //GPS_Date_Stamp
        strcpy((char*)ExifInfo.GPSDatestamp, m_gps_date);

        ExifInfo.hasGps = true;
    } else {
        ExifInfo.hasGps = false;
    }

    ExifSize = mExifCreator->ExifCreate_wo_GPS( (unsigned char*)pOutExifBuf, &ExifInfo, flag);
    OutExifSize = ExifSize;
    delete mExifCreator;
}

void CameraHardware::convertFromDecimalToGPSFormat(double arg1, int& arg2, int& arg3, double& arg4)
{
    double temp1 = 0, temp2 = 0, temp3 = 0;

    arg2  = (int)arg1;
    temp1 = arg1 - arg2;
    temp2 = temp1 * 60;
    arg3  = (int)temp2;
    temp3 = temp2 - arg3;
    arg4  = temp3 * 60;
}

int CameraHardware::convertToExifLMH(int value, int key)
{
    const int NORMAL = 0;
    const int LOW    = 1;
    const int HIGH   = 2;

    value -= key;

    if (value == 0) return NORMAL;
    if (value < 0) return LOW;
    else return HIGH;
}

double CameraHardware::getGPSLatitude() const
{
    double gpsLatitudeValue = 0;

    if (mParameters.get(mParameters.KEY_GPS_LATITUDE)) {
        gpsLatitudeValue = atof(mParameters.get(mParameters.KEY_GPS_LATITUDE));
        if (gpsLatitudeValue != 0) {
            ALOGD("getGPSLatitude = %2.2f \n", gpsLatitudeValue);
        }
        return gpsLatitudeValue;
    } else {
        ALOGD("getGPSLatitude null \n");
        return 0;
    }
}

double CameraHardware::getGPSLongitude() const
{
    double gpsLongitudeValue = 0;

    if (mParameters.get(mParameters.KEY_GPS_LONGITUDE)) {
        gpsLongitudeValue = atof(mParameters.get(mParameters.KEY_GPS_LONGITUDE));
        if (gpsLongitudeValue != 0) {
            ALOGD("getGPSLongitude = %2.2f \n", gpsLongitudeValue);
        }
        return gpsLongitudeValue;
    } else {
        ALOGD("getGPSLongitude null \n");
        return 0;
    }
}

double CameraHardware::getGPSAltitude() const
{
    double gpsAltitudeValue = 0;

    if (mParameters.get(mParameters.KEY_GPS_ALTITUDE)) {
        gpsAltitudeValue = atof(mParameters.get(mParameters.KEY_GPS_ALTITUDE));
        if (gpsAltitudeValue != 0) {
            ALOGD("getGPSAltitude = %2.2f \n", gpsAltitudeValue);
        }
        return gpsAltitudeValue;
    } else {
        ALOGD("getGPSAltitude null \n");
        return 0;
    }
}
}; // namespace android
