include $(all-subdir-makefiles)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
    CameraHal_Module.cpp \
    V4L2Camera.cpp \
    CameraHardware.cpp \
    CameraUtils.cpp \
    converter.cpp \
    ExifCreator.cpp \
    JpegEncoder.c \
    scale.c

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/../include/ \
    system/media/camera/include \
    hardware/ti/omap3/dspbridge/inc \
    hardware/ti/omap3/hwc \
    hardware/ti/omap3/omx/system/src/openmax_il/lcml/inc \
    hardware/ti/omap3/omx/system/src/openmax_il/omx_core/inc \
    hardware/ti/omap3/omx/system/src/openmax_il/common/inc \
    hardware/ti/omap3/omx/image/src/openmax_il/jpeg_enc/inc \

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libbinder \
    libutils \
    libcutils \
    libcamera_client \
    libhardware \
    libexif \
    libOMX_Core \
    libdl \

LOCAL_CFLAGS := -Wall -fpic -pipe -O0 -DOMX_DEBUG=1

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_MODULE_TAGS:= optional
LOCAL_WHOLE_STATIC_LIBRARIES:= libyuv

include $(BUILD_SHARED_LIBRARY)
include $(LOCAL_PATH)/Neon/android.mk
