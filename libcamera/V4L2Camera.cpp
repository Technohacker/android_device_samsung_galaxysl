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

#define LOG_TAG "V4L2Camera"
#include <utils/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
//#include <linux/videodev.h>

extern int version;

#define MEDIA_DEVICE "/dev/media0"
#define ENTITY_VIDEO_CCDC_OUT_NAME      "OMAP3 ISP CCDC output"
#define ENTITY_CCDC_NAME                "OMAP3 ISP CCDC"
#define ENTITY_TVP514X_NAME             "tvp514x 3-005c"
#define ENTITY_MT9T111_NAME             "mt9t111 2-003c"
#ifdef CONFIG_FLASHBOARD
#define ENTITY_MT9V113_NAME             "mt9v113 3-003c"
#else
#define ENTITY_MT9V113_NAME             "mt9v113 2-003c"
#endif
#define IMG_WIDTH_VGA           640
#define IMG_HEIGHT_VGA          480
#define DEF_PIX_FMT             V4L2_PIX_FMT_YUYV

#define FIRST_AF_SEARCH_COUNT 15
#define AF_PROGRESS 0x01
#define AF_SUCCESS 0x02
#define AF_DELAY 300000
#define AF_START 1
#define AF_STOP 2

#define CAMERA_FLIP_NONE 1
#define CAMERA_FLIP_MIRROR 2
#define CAMERA_FLIP_WATER 3
#define CAMERA_FLIP_WATER_MIRROR 4

#define CAMERA_BF 0 //Back Camera
#define CAMERA_FF 1

#include "V4L2Camera.h"

#define OUTPUT_BUF_SIZE 4096 /* choose an efficiently fwrite’able size */


namespace android {

V4L2Camera::V4L2Camera ()
    : nQueued(0), nDequeued(0)
{
    videoIn = (struct vdIn*)calloc(1, sizeof(struct vdIn));
    mediaIn = (struct mdIn*)calloc(1, sizeof(struct mdIn));
    mediaIn->input_source = 1;
    camHandle = -1;
    m_flag_init = 0;
    mZoomLevel = -1;
    mWhiteBalance = -1;
    mFocusMode = -1;
    mSceneMode = -1;
    m_exif_orientation = -1;
    mISO = -1;
    mMetering = -1;
    mContrast = -1;
    mSharpness = -1;
    mSaturation = -1;
    mBrightness = -1;
    mAutofocusRunning = 0;
    mEffect = -1;
    mJpegQuality = -1;
    mCamMode = MODE_CAMERA;

#ifdef _OMAP_RESIZER_
    videoIn->resizeHandle = -1;
#endif          //_OMAP_RESIZER_
}

V4L2Camera::~V4L2Camera()
{
    free(videoIn);
    free(mediaIn);
}

int V4L2Camera::Open(const char *device)
{
    int ret = 0;
    int ccdc_fd, tvp_fd;
    struct v4l2_subdev_format fmt;
    char subdev[20];

    if (!m_flag_init) {
        do {
            if ((camHandle = open(device, O_RDWR)) == -1) {
                ALOGE("ERROR opening V4L interface: %s", strerror(errno));
                return -1;
            }

            ret = ioctl(camHandle, VIDIOC_QUERYCAP, &videoIn->cap);
            if (ret < 0) {
                ALOGE("Error opening device: unable to query device.");
                break;
            }

            if ((videoIn->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
                ALOGE("Error opening device: video capture not supported.");
                ret = -1;
                break;
            }

            if (!(videoIn->cap.capabilities & V4L2_CAP_STREAMING)) {
                ALOGE("Capture device does not support streaming i/o");
                ret = -1;
                break;
            }
#ifdef _OMAP_RESIZER_
            videoIn->resizeHandle = OMAPResizerOpen();
#endif                          //_OMAP_RESIZER_
        } while (0);

        m_flag_init = 1;
    }
    return ret;
}

int V4L2Camera::Open_media_device(const char *device)
{
    int ret = 0;
    int index = 0;
    int i;
    struct media_link_desc link;
    struct media_links_enum links;
    int input_v4l;

    /*opening the media device*/
    mediaIn->media_fd = open(device, O_RDWR);
    if (mediaIn->media_fd <= 0) {
        ALOGE("ERROR opening media device: %s", strerror(errno));
        return -1;
    }

    /*enumerate_all_entities*/
    do {
        mediaIn->entity[index].id = index | MEDIA_ENTITY_ID_FLAG_NEXT;
        ret = ioctl(mediaIn->media_fd, MEDIA_IOC_ENUM_ENTITIES, &mediaIn->entity[index]);
        if (ret < 0) {
            break;
        } else {
            if (!strcmp(mediaIn->entity[index].name, ENTITY_VIDEO_CCDC_OUT_NAME))
                mediaIn->video =  mediaIn->entity[index].id;
            else if (!strcmp(mediaIn->entity[index].name, ENTITY_TVP514X_NAME))
                mediaIn->tvp5146 =  mediaIn->entity[index].id;
            else if (!strcmp(mediaIn->entity[index].name, ENTITY_MT9T111_NAME)) {
                mediaIn->mt9t111 =  mediaIn->entity[index].id;
                mediaIn->input_source = 1;
            } else if (!strcmp(mediaIn->entity[index].name, ENTITY_CCDC_NAME))
                mediaIn->ccdc =  mediaIn->entity[index].id;
            else if (!strcmp(mediaIn->entity[index].name, ENTITY_MT9V113_NAME)) {
                mediaIn->mt9v113 =  mediaIn->entity[index].id;
                mediaIn->input_source = 2;
            }
        }
        index++;
    } while (ret == 0);

    if ((ret < 0) && (index <= 0)) {
        ALOGE("Failed to enumerate entities ret val is %d", ret);
        close(mediaIn->media_fd);
        return -1;
    }
    mediaIn->num_entities = index;

    /*setup_media_links*/
    for (index = 0; index < mediaIn->num_entities; index++) {
        links.entity = mediaIn->entity[index].id;
        links.pads = (struct media_pad_desc*)malloc((sizeof( struct media_pad_desc)) * (mediaIn->entity[index].pads));
        links.links = (struct media_link_desc*)malloc((sizeof(struct media_link_desc)) * mediaIn->entity[index].links);
        ret = ioctl(mediaIn->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
        if (ret < 0) {
            ALOGE("ERROR  while enumerating links/pads");
            break;
        } else {
            if (mediaIn->entity[index].pads)
                ALOGD("pads for entity %d=", mediaIn->entity[index].id);
            for (i = 0; i < mediaIn->entity[index].pads; i++) {
                ALOGD("(%d %s) ", links.pads->index, (links.pads->flags & MEDIA_PAD_FLAG_INPUT) ? "INPUT" : "OUTPUT");
                links.pads++;
            }
            for (i = 0; i < mediaIn->entity[index].links; i++) {
                ALOGD("[%d:%d]===>[%d:%d]", links.links->source.entity, links.links->source.index, links.links->sink.entity, links.links->sink.index);
                if (links.links->flags & MEDIA_LINK_FLAG_ENABLED)
                    ALOGD("\tACTIVE\n");
                else
                    ALOGD("\tINACTIVE \n");
                links.links++;
            }
        }
    }
    if (mediaIn->input_source == 1)
        input_v4l = mediaIn->mt9t111;
    else if (mediaIn->input_source == 2)
        input_v4l = mediaIn->mt9v113;
    else
        input_v4l = mediaIn->tvp5146;

    memset(&link, 0, sizeof(link));
    link.flags |=  MEDIA_LINK_FLAG_ENABLED;
    link.source.entity = input_v4l;
    link.source.index = 0;

    link.source.flags = MEDIA_PAD_FLAG_OUTPUT;
    link.sink.entity = mediaIn->ccdc;
    link.sink.index = 0;
    link.sink.flags = MEDIA_PAD_FLAG_INPUT;

    ret = ioctl(mediaIn->media_fd, MEDIA_IOC_SETUP_LINK, &link);
    if (ret) {
        ALOGE("Failed to enable link bewteen entities");
        close(mediaIn->media_fd);
        return -1;
    }
    memset(&link, 0, sizeof(link));
    link.flags |=  MEDIA_LINK_FLAG_ENABLED;
    link.source.entity = mediaIn->ccdc;
    link.source.index = 1;
    link.source.flags = MEDIA_PAD_FLAG_OUTPUT;
    link.sink.entity = mediaIn->video;
    link.sink.index = 0;
    link.sink.flags = MEDIA_PAD_FLAG_INPUT;
    ret = ioctl(mediaIn->media_fd, MEDIA_IOC_SETUP_LINK, &link);
    if (ret) {
        ALOGE("Failed to enable link");

        close(mediaIn->media_fd);
        return -1;
    }

    /*close media device*/
    close(mediaIn->media_fd);
    return 0;
}

int V4L2Camera::Configure(int width, int height, int pixelformat, int fps, int cam_state)
{
    int ret = 0;
    struct v4l2_streamparm parm;

    /*dhiru1602 : use cam_state to determine if the camera is in Preview or
            Capture mode */
    setFramerate(fps, cam_state);
    videoIn->width = width;
    videoIn->height = height;
    videoIn->framesizeIn = ((width * height) << 1);
    videoIn->formatIn = pixelformat;
    videoIn->format.fmt.pix.width = width;
    videoIn->format.fmt.pix.height = height;
    videoIn->format.fmt.pix.pixelformat = pixelformat;
    videoIn->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    do {
        ret = ioctl(camHandle, VIDIOC_S_FMT, &videoIn->format);
        if (ret < 0) {
            ALOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
            break;
        }
        ALOGD("CameraConfigure PreviewFormat: w=%d h=%d", videoIn->format.fmt.pix.width, videoIn->format.fmt.pix.height);

    } while (0);

    return ret;
}

int V4L2Camera::setCamMode(int cam_mode)
{
    switch (cam_mode) {
    case MODE_CAMERA:
        ALOGV("%s: MODE_CAMERA(%d)", __func__, cam_mode);
        mCamMode = MODE_CAMERA;
        break;
    case MODE_CAMCORDER:
        ALOGV("%s: MODE_CAMCORDER(%d)", __func__, cam_mode);
        mCamMode = MODE_CAMCORDER;
        break;
    case MODE_VT:
        ALOGV("%s: MODE_VT(%d)", __func__, cam_mode);
        mCamMode = MODE_VT;
        break;
    default:
        ALOGE("%s: mode %d no supported", __func__, cam_mode);
        return -1;
    }
    return 0;
}

int V4L2Camera::getCamMode(void)
{
    return mCamMode;
}

int V4L2Camera::setFramerate(int framerate, int cam_state)
{
    int ret;
    struct v4l2_streamparm parm;

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.capturemode = mCamMode;
    if (cam_state == STATE_PREVIEW)
        parm.parm.capture.currentstate = V4L2_MODE_PREVIEW;
    else
        parm.parm.capture.currentstate = V4L2_MODE_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = framerate;
    ret = ioctl(camHandle, VIDIOC_S_PARM, &parm);
    if (ret != 0) {
        ALOGE("error in SetFramerate: VIDIOC_S_PARM  Fail...., error(%s)", strerror(errno));
        return -1;
    }
    return 0;
}

int V4L2Camera::BufferMap(int cam_state)
{
    int ret, mMaxBuffers;

    //dhiru1602: If the camera is in Capture Mode, use only a Single Buffer
    if (cam_state == STATE_PREVIEW)
        mMaxBuffers = NB_BUFFER;
    else
        mMaxBuffers = 1;

    /* Check if camera can handle mMaxBuffers buffers */
    videoIn->rb.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->rb.memory  = V4L2_MEMORY_MMAP;
    videoIn->rb.count   = mMaxBuffers;

    ret = ioctl(camHandle, VIDIOC_REQBUFS, &videoIn->rb);
    if (ret < 0) {
        ALOGE("Init: VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }
    for (int i = 0; i < mMaxBuffers; i++) {
        memset(&videoIn->buf, 0, sizeof(struct v4l2_buffer));

        videoIn->buf.index = i;
        videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        videoIn->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(camHandle, VIDIOC_QUERYBUF, &videoIn->buf);
        if (ret < 0) {
            ALOGE("Init: Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        videoIn->mem[i] = mmap(NULL,
                       videoIn->buf.length,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       camHandle,
                       videoIn->buf.m.offset);

        if (videoIn->mem[i] == MAP_FAILED) {
            ALOGE("Init: Unable to map buffer (%s)", strerror(errno));
            return -1;
        }

        ret = ioctl(camHandle, VIDIOC_QBUF, &videoIn->buf);
        if (ret < 0) {
            ALOGE("Init: VIDIOC_QBUF Failed");
            return -1;
        }

        nQueued++;
    }

    return 0;
}

void V4L2Camera::reset_links(const char *device)
{
    struct media_link_desc link;
    struct media_links_enum links;
    int ret, index, i;

    /*reset the media links*/
    mediaIn->media_fd = open(device, O_RDWR);
    for (index = 0; index < mediaIn->num_entities; index++) {
        links.entity = mediaIn->entity[index].id;
        links.pads = (struct media_pad_desc*)malloc(sizeof( struct media_pad_desc) * mediaIn->entity[index].pads);
        links.links = (struct media_link_desc*)malloc(sizeof(struct media_link_desc) * mediaIn->entity[index].links);
        ret = ioctl(mediaIn->media_fd, MEDIA_IOC_ENUM_LINKS, &links);
        if (ret < 0) {
            ALOGE("Error while enumeration links/pads - %d\n", ret);
            break;
        } else {
            for (i = 0; i < mediaIn->entity[index].links; i++) {
                link.source.entity = links.links->source.entity;
                link.source.index = links.links->source.index;
                link.source.flags = MEDIA_PAD_FLAG_OUTPUT;
                link.sink.entity = links.links->sink.entity;
                link.sink.index = links.links->sink.index;
                link.sink.flags = MEDIA_PAD_FLAG_INPUT;
                link.flags = (link.flags & ~MEDIA_LINK_FLAG_ENABLED) | (link.flags & MEDIA_LINK_FLAG_IMMUTABLE);
                ret = ioctl(mediaIn->media_fd, MEDIA_IOC_SETUP_LINK, &link);
                if (ret)
                    break;
                links.links++;
            }
        }
    }
    close(mediaIn->media_fd);
}

void V4L2Camera::Close()
{
    if (m_flag_init) {
        close(camHandle);
        camHandle = NULL;
        ALOGD("Camera Driver Closed");
#ifdef _OMAP_RESIZER_
        OMAPResizerClose(videoIn->resizeHandle);
        videoIn->resizeHandle = -1;
#endif                  //_OMAP_RESIZER_
        m_flag_init = 0;
    }
    return;
}

int V4L2Camera::init_parm()
{
    int ret;
    int framerate;
    struct v4l2_streamparm parm;

    framerate = DEFAULT_FRAME_RATE;
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(camHandle, VIDIOC_G_PARM, &parm);
    if (ret != 0) {
        ALOGE("VIDIOC_G_PARM fail....");
        return ret;
    }

    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = framerate;
    ret = ioctl(camHandle, VIDIOC_S_PARM, &parm);
    if (ret != 0) {
        ALOGE("VIDIOC_S_PARM  Fail....");
        return -1;
    }

    return 0;
}

void V4L2Camera::Uninit(int cam_state)
{
    int ret, mMaxBuffers;
    if (m_flag_init) {
        //dhiru1602: If the camera is in Capture Mode, use only a Single Buffer
        if (cam_state == STATE_PREVIEW)
            mMaxBuffers = NB_BUFFER;
        else
            mMaxBuffers = 1;

        videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        videoIn->buf.memory = V4L2_MEMORY_MMAP;

        /* Dequeue everything */
        int DQcount = nQueued - nDequeued;

        for (int i = 0; i < DQcount - 1; i++) {
            ret = ioctl(camHandle, VIDIOC_DQBUF, &videoIn->buf);
            if (ret < 0)
                ALOGE("Uninit: VIDIOC_DQBUF Failed");
        }
        nQueued = 0;
        nDequeued = 0;

        /* Unmap buffers */
        for (int i = 0; i < mMaxBuffers; i++)
            munmap(videoIn->mem[i], videoIn->buf.length);
    }
    return;
}

int V4L2Camera::StartStreaming(int cam_state)
{
    enum v4l2_buf_type type;
    struct v4l2_control vc;
    int ret;

    if (!videoIn->isStreaming) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(camHandle, VIDIOC_STREAMON, &type);
        if (ret < 0) {
            ALOGE("StartStreaming: Unable to start capture: %s", strerror(errno));
            return ret;
        }

        if (cam_state == STATE_PREVIEW)
            videoIn->isStreaming = true;
    }

    return 0;
}

int V4L2Camera::StopStreaming(int cam_state)
{
    enum v4l2_buf_type type;
    int ret;

    if (videoIn->isStreaming || cam_state == STATE_PICTURE) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(camHandle, VIDIOC_STREAMOFF, &type);
        if (ret < 0) {
            ALOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        videoIn->isStreaming = false;
    }

    return 0;
}

void* V4L2Camera::GrabFrame(int& index)
{
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    /* DQ */
    ret = ioctl(camHandle, VIDIOC_DQBUF, &videoIn->buf);
    if (ret < 0) {
        ALOGE("GrabRecordFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    nDequeued++;

    index = videoIn->buf.index;

    return(videoIn->mem[videoIn->buf.index] );
}

void V4L2Camera::ReleaseFrame(int index)
{
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;
    videoIn->buf.index = index;

    ret = ioctl(camHandle, VIDIOC_QBUF, &videoIn->buf);
    nQueued++;
    if (ret < 0) {
        ALOGE("ReleaseRecordFrame: VIDIOC_QBUF Failed");
        return;
    }
    return;
}

void V4L2Camera::GrabRawFrame(void *previewBuffer, unsigned int width, unsigned int height)
{
    int ret = 0;
    int DQcount = 0;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    DQcount = nQueued - nDequeued;
    if (DQcount == 0) {
        ALOGV("postGrabRawFrame: Drop the frame");
        ret = ioctl(camHandle, VIDIOC_QBUF, &videoIn->buf);
        if (ret < 0) {
            ALOGE("postGrabRawFrame: VIDIOC_QBUF Failed");
            return;
        }
    }

    /* DQ */
    ret = ioctl(camHandle, VIDIOC_DQBUF, &videoIn->buf);
    if (ret < 0) {
        ALOGE("GrabRawFrame: VIDIOC_DQBUF Failed");
        return;
    }
    nDequeued++;

    if (videoIn->format.fmt.pix.width != width || \
        videoIn->format.fmt.pix.height != height) {
        /* do resize */
        ALOGV("Resizing required");
#ifdef _OMAP_RESIZER_
        ret = OMAPResizerConvert(videoIn->resizeHandle, videoIn->mem[videoIn->buf.index], \
                     videoIn->format.fmt.pix.height, \
                     videoIn->format.fmt.pix.width,    \
                     previewBuffer,    \
                     height, \
                     width);
        if (ret < 0)
            ALOGE("Resize operation:%d", ret);
#endif                  //_OMAP_RESIZER_
    } else {
        memcpy(previewBuffer, videoIn->mem[videoIn->buf.index], (size_t)videoIn->buf.bytesused);
    }

    ret = ioctl(camHandle, VIDIOC_QBUF, &videoIn->buf);
    if (ret < 0) {
        ALOGE("postGrabRawFrame: VIDIOC_QBUF Failed");
        return;
    }

    nQueued++;
}

void* V4L2Camera::GrabJpegFrame(int& mfilesize, bool IsFrontCam)
{
    unsigned long fileSize = 0;
    int ret;

    videoIn->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    videoIn->buf.memory = V4L2_MEMORY_MMAP;

    do {
        ALOGV("GrabJpegFrame - Dequeue buffer");
        /* Dequeue buffer */
        ret = ioctl(camHandle, VIDIOC_DQBUF, &videoIn->buf);
        if (ret < 0) {
            ALOGE("GrabJpegFrame: VIDIOC_DQBUF Failed");
            break;
        }
        nDequeued++;

        ALOGV("GrabJpegFrame - Enqueue buffer");

        /* Enqueue buffer */
        ret = ioctl(camHandle, VIDIOC_QBUF, &videoIn->buf);
        if (ret < 0) {
            ALOGE("GrabJpegFrame: VIDIOC_QBUF Failed");
            break;
        }
        nQueued++;

        //Latona Front Camera doesn't support Image Processing. Manually Encode the JPEG
        if (IsFrontCam) {
            ALOGV("YUVU Format");
            mfilesize =  videoIn->width * videoIn->height * 2;
        } else {
            mfilesize = GetJpegImageSize();
        }
        break;
    } while (0);

    return (videoIn->mem[videoIn->buf.index]);
}

int V4L2Camera::GetJpegImageSize()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_JPEG_SIZE;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.\n");
        return -1;
    }
    return (vc.value);
}

int V4L2Camera::GetThumbNailOffset()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_FW_THUMBNAIL_OFFSET;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.\n");
        return -1;
    }
    return(vc.value);
}

int V4L2Camera::GetYUVOffset()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_FW_YUV_OFFSET;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.");
        return -1;
    }
    return(vc.value);
}

int V4L2Camera::GetThumbNailDataSize()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_THUMBNAIL_SIZE;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.");
        return -1;
    }
    return(vc.value);
}

int V4L2Camera::GetJPEG_Capture_Width()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_JPEG_CAPTURE_WIDTH;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.\n");
        return -1;
    }
    return(vc.value);
}

int V4L2Camera::GetJPEG_Capture_Height()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_JPEG_CAPTURE_HEIGHT;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get VIDIOC_G_CTRL.\n");
        return -1;
    }
    return(vc.value);
}

int V4L2Camera::GetCamera_version()
{
    struct v4l2_control vc;
    vc.id = V4L2_CID_FW_VERSION;
    vc.value = 0;
    if (ioctl(camHandle, VIDIOC_G_CTRL, &vc) < 0) {
        ALOGE("Failed to get V4L2_CID_FW_VERSION.\n");
    }

    return (vc.value);
}


void V4L2Camera::getExifInfoFromDriver(v4l2_exif* exifobj)
{
    if (ioctl(camHandle, VIDIOC_G_EXIF, exifobj) < 0) {
        ALOGE("Failed to get vidioc_g_exif.\n");
    }
}

static inline void yuv_to_rgb16(unsigned char y,
                unsigned char u,
                unsigned char v,
                unsigned char *rgb)
{
    register int r, g, b;
    int rgb16;

    r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
    g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u - 128) ) >> 10;
    b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;

    rgb16 = (int)(((r >> 3) << 11) | ((g >> 2) << 5) | ((b >> 3) << 0));

    *rgb = (unsigned char)(rgb16 & 0xFF);
    rgb++;
    *rgb = (unsigned char)((rgb16 & 0xFF00) >> 8);
}

// IOCTL Control Interface
static int v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;
    ctrl.value = value;

    ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d) failed ret = %d\n",
              __func__, id, id - V4L2_CID_PRIVATE_BASE, value, ret);

        return ret;
    }

    return ctrl.value;
}

static int v4l2_g_ctrl(int fp, unsigned int id)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;

    ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s): VIDIOC_G_CTRL(id = 0x%x (%d)) failed, ret = %d\n",
              __func__, id, id - V4L2_CID_PRIVATE_BASE, ret);
        return ret;
    }

    return ctrl.value;
}

// Autofocus Functions
int V4L2Camera::setAutofocus(void)
{
    ALOGV("%s :", __func__);

    if (camHandle <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    if (videoIn->isStreaming) {
        if (v4l2_s_ctrl(camHandle, V4L2_CID_AF, AF_START) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __func__);
            return -1;
        }
    }

    mAutofocusRunning = 1;

    return 0;
}

int V4L2Camera::getAutoFocusResult(void)
{
    int af_result, count, ret;

    for (count = 0; count < FIRST_AF_SEARCH_COUNT; count++) {
        ret = v4l2_g_ctrl(camHandle, V4L2_CID_AF);
        if (ret != AF_PROGRESS)
            break;
        usleep(AF_DELAY);
    }

    if ((count >= FIRST_AF_SEARCH_COUNT) || (ret != AF_SUCCESS)) {
        ALOGV("%s : 1st AF timed out, failed, or was canceled", __func__);
        af_result = 0;
        goto finish_auto_focus;
    }

    af_result = 1;
    ALOGV("%s : AF was successful, returning %d", __func__, af_result);

finish_auto_focus:
    mAutofocusRunning = 0;
    return af_result;
}

int V4L2Camera::cancelAutofocus(void)
{
    ALOGV("%s :", __func__);

    if (camHandle <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    /* AF_STOP restores the sensor focus state to default. Hence,
       we only use AF_STOP only when Autofocus is currently running */
    if (!mAutofocusRunning && videoIn->isStreaming) {
        if (v4l2_s_ctrl(camHandle, V4L2_CID_AF, AF_STOP) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_SET_AUTO_FOCUS", __func__);
            return -1;
        }
    }

    return 0;
}

// End of Autofocus

// Touch Focus

int V4L2Camera::setObjectPosition(int x, int y)
{
    ALOGV("%s(setObjectPosition(x=%d, y=%d))", __func__, x, y);

    if (videoIn->isStreaming) {
        if (v4l2_s_ctrl(camHandle, V4L2_CID_CAMERA_OBJECT_POSITION_X, x) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_OBJECT_POSITION_X", __func__);
            return -1;
        }

        if (v4l2_s_ctrl(camHandle, V4L2_CID_CAMERA_OBJECT_POSITION_Y, y) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_OBJECT_POSITION_Y", __func__);
            return -1;
        }
    }

    return 0;
}

int V4L2Camera::setTouchAFStartStop(int start_stop)
{
    ALOGV("%s(touch_af_start_stop (%d))", __func__, start_stop);

    if (videoIn->isStreaming) {
        // We need to send this command regardless of previous state to trigger AF
        if (v4l2_s_ctrl(camHandle, V4L2_CID_CAMERA_TOUCH_AF_START_STOP, start_stop) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_TOUCH_AF_START_STOP", __func__);
            return -1;
        }
    }

    return 0;
}

// Zoom Setting
int V4L2Camera::setZoom(int zoom_level)
{
    ALOGV("%s(zoom_level (%d))", __func__, zoom_level);

    if (zoom_level < ZOOM_LEVEL_0 || ZOOM_LEVEL_MAX <= zoom_level) {
        ALOGE("ERR(%s):Invalid zoom_level (%d)", __func__, zoom_level);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mZoomLevel != zoom_level) {
            mZoomLevel = zoom_level;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_ZOOM, zoom_level) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_ZOOM", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::setWhiteBalance(int white_balance)
{
    ALOGV("%s(white_balance(%d))", __func__, white_balance);

    if (white_balance <= WHITE_BALANCE_BASE || WHITE_BALANCE_MAX <= white_balance) {
        ALOGE("ERR(%s):Invalid white_balance(%d)", __func__, white_balance);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mWhiteBalance != white_balance) {
            mWhiteBalance = white_balance;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_WB, white_balance) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_WB", __func__);
                return -1;
            }
        }
    //}
    return 0;
}

int V4L2Camera::getWhiteBalance()
{
    return (mWhiteBalance);
}

int V4L2Camera::setSceneMode(int scene_mode)
{
    ALOGV("%s(scene_mode(%d))", __func__, scene_mode);

    if (scene_mode <= SCENE_MODE_BASE || SCENE_MODE_MAX <= scene_mode) {
        ALOGE("ERR(%s):Invalid scene_mode (%d)", __func__, scene_mode);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mSceneMode != scene_mode) {
            mSceneMode = scene_mode;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_SCENE, scene_mode) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_SCENE", __func__);
                return -1;
            }
        }
    //}
    return 0;
}

int V4L2Camera::getSceneMode()
{
    return (mSceneMode);
}

int V4L2Camera::setFocusMode(int focus_mode)
{
    ALOGV("%s(focus_mode(%d))", __func__, focus_mode);

    if (FOCUS_MODE_MAX <= focus_mode) {
        ALOGE("ERR(%s):Invalid focus_mode (%d)", __func__, focus_mode);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mFocusMode != focus_mode) {
            mFocusMode = focus_mode;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_FOCUS_MODE, focus_mode) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_FOCUS_MODE", __func__);
                return -1;
            }
        }
    //}
    return 0;
}

int V4L2Camera::setAEAWBLockUnlock(int ae_lockunlock, int awb_lockunlock)
{
    int ae_awb_status = 0;

    struct v4l2_control vc;

    if (ae_lockunlock == 0 && awb_lockunlock == 0)
        ae_awb_status = AE_UNLOCK_AWB_UNLOCK;
    else if (ae_lockunlock == 1 && awb_lockunlock == 0)
        ae_awb_status = AE_LOCK_AWB_UNLOCK;
    else if (ae_lockunlock == 0 && awb_lockunlock == 1)
        ae_awb_status = AE_UNLOCK_AWB_LOCK;
    else
        ae_awb_status = AE_LOCK_AWB_LOCK;

    vc.id = V4L2_CID_CAMERA_AE_AWB_LOCKUNLOCK;
    vc.value = ae_awb_status;

    if (ioctl(camHandle, VIDIOC_S_CTRL, &vc) < 0) {
        ALOGE("ERR(%s):Fail on V4L2_CID_CAMERA_AE_AWB_LOCKUNLOCK", __func__);
        return -1;
    }

    return 0;
}

int V4L2Camera::SetCameraFlip(bool isCapture)
{
    struct v4l2_control vc;

    vc.id = V4L2_CID_FLIP;
    if (isCapture)
        vc.value = CAMERA_FLIP_NONE;
    else
        vc.value = CAMERA_FLIP_MIRROR;
    if (ioctl(camHandle, VIDIOC_S_CTRL, &vc) < 0) {
        ALOGE("V4L2_CID_FLIP fail!\n");
        return UNKNOWN_ERROR;
    }
    return 0;
}

int V4L2Camera::setExifOrientationInfo(int orientationInfo)
{
    if (orientationInfo < 0) {
        ALOGE("ERR(%s):Invalid orientationInfo (%d)", __func__, orientationInfo);
        return -1;
    }
    m_exif_orientation = orientationInfo;

    return 0;
}

// ===============================================================================================

int V4L2Camera::setISO(int iso_value)
{
    if (iso_value < ISO_AUTO || ISO_MAX <= iso_value) {
        ALOGE("ERR(%s):Invalid iso_value (%d)", __func__, iso_value);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mISO != iso_value) {
            mISO = iso_value;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_ISO, iso_value) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_ISO", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getISO(void)
{
    return (mISO);
}

// ===============================================================================================

int V4L2Camera::setMetering(int metering_value)
{
    if (metering_value <= METERING_BASE || METERING_MAX <= metering_value) {
        ALOGE("ERR(%s):Invalid metering_value (%d)", __func__, metering_value);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mMetering != metering_value) {
            mMetering = metering_value;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_PHOTOMETRY, metering_value) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_PHOTOMETRY", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getMetering(void)
{
    return (mMetering);
}

// ===============================================================================================

int V4L2Camera::setContrast(int contrast_value)
{
    ALOGV("%s(contrast_value(%d))", __func__, contrast_value);

    if (contrast_value < CONTRAST_MINUS_2 || CONTRAST_MAX <= contrast_value) {
        ALOGE("ERR(%s):Invalid contrast_value (%d)", __func__, contrast_value);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mContrast != contrast_value) {
            mContrast = contrast_value;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_CONTRAST, contrast_value) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_CONTRAST", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getContrast(void)
{
    return (mContrast);
}

// ===============================================================================================

int V4L2Camera::setSharpness(int sharpness_value)
{
    ALOGV("%s(sharpness_value(%d))", __func__, sharpness_value);

    if (sharpness_value < SHARPNESS_MINUS_2 || SHARPNESS_MAX <= sharpness_value) {
        ALOGE("ERR(%s):Invalid sharpness_value (%d)", __func__, sharpness_value);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mSharpness != sharpness_value) {
            mSharpness = sharpness_value;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_SHARPNESS, sharpness_value) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_SHARPNESS", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getSharpness(void)
{
    return (mSharpness);
}

// ===============================================================================================

int V4L2Camera::setSaturation(int saturation_value)
{
    if (saturation_value < SATURATION_MINUS_2 || SATURATION_MAX <= saturation_value) {
        ALOGE("ERR(%s):Invalid saturation_value (%d)", __func__, saturation_value);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mSaturation != saturation_value) {
            mSaturation = saturation_value;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_SATURATION, saturation_value) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_SATURATION", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getSaturation(void)
{
    return (mSaturation);
}

// ===============================================================================================

int V4L2Camera::setBrightness(int brightness)
{
    ALOGV("%s(brightness(%d))", __func__, brightness);

    brightness += EV_DEFAULT;

    if (brightness < EV_MINUS_4 || EV_PLUS_4 < brightness) {
        ALOGE("ERR(%s):Invalid brightness(%d)", __func__, brightness);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mBrightness != brightness) {
            mBrightness = brightness;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_BRIGHTNESS, brightness) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_BRIGHTNESS", __func__);
                return -1;
            }
        }
    //}
    return 0;
}

int V4L2Camera::getBrightness(void)
{
    return (mBrightness);
}

// -----------------------------------
int V4L2Camera::setImageEffect(int image_effect)
{
    ALOGV("%s(image_effect(%d))", __func__, image_effect);

    if (image_effect <= CAMERA_EFFECT_BASE || CAMERA_EFFECT_MAX <= image_effect) {
        ALOGE("ERR(%s):Invalid image_effect(%d)", __func__, image_effect);
        return -1;
    }

    //if (videoIn->isStreaming) {
        if (mEffect != image_effect) {
            mEffect = image_effect;
            if (v4l2_s_ctrl(camHandle, V4L2_CID_EFFECT, image_effect) < 0) {
                ALOGE("ERR(%s):Fail on V4L2_CID_EFFECT", __func__);
                return -1;
            }
        }
    //}

    return 0;
}

int V4L2Camera::getImageEffect(void)
{
    ALOGV("%s : image_effect(%d)", __func__, mEffect);
    return mEffect;
}

//======================================================================

int V4L2Camera::setJpegQuality(int jpeg_quality)
{
    ALOGV("%s(jpeg_quality (%d))", __func__, jpeg_quality);

    if (jpeg_quality < JPEG_QUALITY_SUPERFINE || JPEG_QUALITY_MAX <= jpeg_quality) {
        ALOGE("ERR(%s):Invalid jpeg_quality (%d)", __func__, jpeg_quality);
        return -1;
    }

    if (mJpegQuality != jpeg_quality) {
        mJpegQuality = jpeg_quality;
        if (v4l2_s_ctrl(camHandle, V4L2_CID_JPEG_QUALITY, jpeg_quality) < 0) {
            ALOGE("ERR(%s):Fail on V4L2_CID_JPEG_QUALITY", __func__);
            return -1;
        }
    }

    return 0;
}

int V4L2Camera::getJpegQuality(void)
{
    return mJpegQuality;
}

// ===============================================================================================

int V4L2Camera::getOrientation()
{
    return (m_exif_orientation);
}

void V4L2Camera::stopPreview()
{
    Uninit(0);
    StopStreaming(0);
}

void V4L2Camera::convert(unsigned char *buf, unsigned char *rgb, int width, int height)
{
    int x, y, z = 0;
    int blocks;

    blocks = (width * height) * 2;

    for (y = 0; y < blocks; y += 4) {
        unsigned char Y1, Y2, U, V;

        U = buf[y + 0];
        Y1 = buf[y + 1];
        V = buf[y + 2];
        Y2 = buf[y + 3];

        yuv_to_rgb16(Y1, U, V, &rgb[y]);
        yuv_to_rgb16(Y2, U, V, &rgb[y + 2]);
    }
}

}; // namespace android
