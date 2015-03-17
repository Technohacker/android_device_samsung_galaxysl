# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# These are the hardware-specific configuration files
PRODUCT_COPY_FILES := \
	device/samsung/galaxysl/etc/gps.conf:system/etc/gps.conf \
	device/samsung/galaxysl/etc/gps.xml:system/vendor/etc/gps.xml

# Init files
PRODUCT_COPY_FILES += \
	device/samsung/galaxysl/init.latona.rc:root/init.latona.rc \
	device/samsung/galaxysl/init.latona.usb.rc:root/init.latona.usb.rc \
	device/samsung/galaxysl/fstab.latona:root/fstab.latona \
	device/samsung/galaxysl/twrp.fstab:recovery/root/etc/twrp.fstab \
	device/samsung/galaxysl/lpm.rc:root/lpm.rc \
	device/samsung/galaxysl/lpm.rc:recovery/root/lpm.rc \
	device/samsung/galaxysl/ueventd.latona.rc:root/ueventd.latona.rc \
	device/samsung/galaxysl/ueventd.latona.rc:recovery/root/ueventd.latona.rc \
	device/samsung/galaxysl/init.recovery.latona.rc:root/init.recovery.latona.rc \
	device/samsung/galaxysl/init.latona.usb.rc:recovery/root/usb.rc

# Configuration files for audio
PRODUCT_COPY_FILES += \
	device/samsung/galaxysl/etc/audio/aeqcoe.txt:system/etc/audio/aeqcoe.txt \
	device/samsung/galaxysl/etc/audio/LVVEFS_Rx_Configuration.txt:system/etc/audio/LVVEFS_Rx_Configuration.txt \
	device/samsung/galaxysl/etc/audio/LVVEFS_Tx_Configuration.txt:system/etc/audio/LVVEFS_Tx_Configuration.txt \
	device/samsung/galaxysl/etc/audio/Rx_ControlParams_EARPIECE_WIDEBAND.txt:system/etc/audio/Rx_ControlParams_EARPIECE_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Rx_ControlParams_SPEAKER_WIDEBAND.txt:system/etc/audio/Rx_ControlParams_SPEAKER_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Rx_ControlParams_WIRED_HEADPHONE_WIDEBAND.txt:system/etc/audio/Rx_ControlParams_WIRED_HEADPHONE_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Rx_ControlParams_WIRED_HEADSET_WIDEBAND.txt:system/etc/audio/Rx_ControlParams_WIRED_HEADSET_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/situation.txt:system/etc/audio/situation.txt \
	device/samsung/galaxysl/etc/audio/soundbooster.txt:system/etc/audio/soundbooster.txt \
	device/samsung/galaxysl/etc/audio/srstunning.txt:system/etc/audio/srstunning.txt \
	device/samsung/galaxysl/etc/audio/stream_earpiece.txt:system/etc/audio/stream_earpiece.txt \
	device/samsung/galaxysl/etc/audio/stream_headset.txt:system/etc/audio/stream_headset.txt \
	device/samsung/galaxysl/etc/audio/stream_speaker.txt:system/etc/audio/stream_speaker.txt \
	device/samsung/galaxysl/etc/audio/Tx_ControlParams_EARPIECE_WIDEBAND.txt:system/etc/audio/Tx_ControlParams_EARPIECE_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Tx_ControlParams_SPEAKER_WIDEBAND.txt:system/etc/audio/Tx_ControlParams_SPEAKER_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Tx_ControlParams_WIRED_HEADPHONE_WIDEBAND.txt:system/etc/audio/Tx_ControlParams_WIRED_HEADPHONE_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/Tx_ControlParams_WIRED_HEADSET_WIDEBAND.txt:system/etc/audio/Tx_ControlParams_WIRED_HEADSET_WIDEBAND.txt \
	device/samsung/galaxysl/etc/audio/codec/FMRadioEar.ini:system/etc/audio/codec/FMRadioEar.ini \
	device/samsung/galaxysl/etc/audio/codec/FMRadioSpk.ini:system/etc/audio/codec/FMRadioSpk.ini \
	device/samsung/galaxysl/etc/audio/codec/MusicEar.ini:system/etc/audio/codec/MusicEar.ini \
	device/samsung/galaxysl/etc/audio/codec/MusicEarAmp.ini:system/etc/audio/codec/MusicEarAmp.ini \
	device/samsung/galaxysl/etc/audio/codec/MusicSpk.ini:system/etc/audio/codec/MusicSpk.ini \
	device/samsung/galaxysl/etc/audio/codec/MusicSpkAmp.ini:system/etc/audio/codec/MusicSpkAmp.ini \
	device/samsung/galaxysl/etc/audio/codec/RecHeadSetMic.ini:system/etc/audio/codec/RecHeadSetMic.ini \
	device/samsung/galaxysl/etc/audio/codec/RecMainMic.ini:system/etc/audio/codec/RecMainMic.ini \
	device/samsung/galaxysl/etc/audio/codec/RecSubMic.ini:system/etc/audio/codec/RecSubMic.ini \
	device/samsung/galaxysl/etc/audio/codec/RingtoneEar.ini:system/etc/audio/codec/RingtoneEar.ini \
	device/samsung/galaxysl/etc/audio/codec/RingtoneSpk.ini:system/etc/audio/codec/RingtoneSpk.ini \
	device/samsung/galaxysl/etc/audio/codec/VCall3pEar.ini:system/etc/audio/codec/VCall3pEar.ini \
	device/samsung/galaxysl/etc/audio/codec/VCall4pEar.ini:system/etc/audio/codec/VCall4pEar.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCall3pEar.ini:system/etc/audio/codec/VoiceCall3pEar.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCall4pEar.ini:system/etc/audio/codec/VoiceCall4pEar.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCallBT.ini:system/etc/audio/codec/VoiceCallBT.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCallEarAmp.ini:system/etc/audio/codec/VoiceCallEarAmp.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCallRcv.ini:system/etc/audio/codec/VoiceCallRcv.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCallSpk.ini:system/etc/audio/codec/VoiceCallSpk.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceCallSpkAmp.ini:system/etc/audio/codec/VoiceCallSpkAmp.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceRecHeadSetMic.ini:system/etc/audio/codec/VoiceRecHeadSetMic.ini \
	device/samsung/galaxysl/etc/audio/codec/VoiceRecMainMic.ini:system/etc/audio/codec/VoiceRecMainMic.ini \
	device/samsung/galaxysl/etc/audio/codec/VtCallBT.ini:system/etc/audio/codec/VtCallBT.ini \
	device/samsung/galaxysl/etc/audio/codec/VtCallEarAmp.ini:system/etc/audio/codec/VtCallEarAmp.ini \
	device/samsung/galaxysl/etc/audio/codec/VtCallRcv.ini:system/etc/audio/codec/VtCallRcv.ini \
	device/samsung/galaxysl/etc/audio/codec/VtCallSpk.ini:system/etc/audio/codec/VtCallSpk.ini \
	device/samsung/galaxysl/etc/audio/codec/VtCallSpkAmp.ini:system/etc/audio/codec/VtCallSpkAmp.ini

# configuration files
PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:system/etc/media_codecs_google_telephony.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:system/etc/media_codecs_google_video.xml \
    device/samsung/galaxysl/etc/media_profiles.xml:system/etc/media_profiles.xml \
    device/samsung/galaxysl/etc/media_codecs.xml:system/etc/media_codecs.xml

# WIFI Configs
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/etc/wifi/p2p_supplicant_overlay.conf:system/etc/wifi/p2p_supplicant_overlay.conf

#Bluetooth configuration files
PRODUCT_COPY_FILES += \
    device/samsung/galaxysl/etc/bluetooth/main.conf:system/etc/bluetooth/main.conf

# Prebuilt kl keymaps
PRODUCT_COPY_FILES += \
       device/samsung/galaxysl/usr/keylayout/twl4030-keypad.kl:system/usr/keylayout/twl4030-keypad.kl \
       device/samsung/galaxysl/usr/keylayout/zeus_key.kl:system/usr/keylayout/zeus_key.kl \
       device/samsung/galaxysl/usr/keylayout/atmel_mxt_ts.kl:system/usr/keylayout/atmel_mxt_ts.kl
 
 # IDC file for Touchscreen Calibration
PRODUCT_COPY_FILES += \
       device/samsung/galaxysl/usr/idc/atmel_mxt_ts.idc:system/usr/idc/atmel_mxt_ts.idc

# These are the hardware-specific features
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
	frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
	frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
	frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
	frameworks/native/data/etc/android.hardware.location.xml:system/etc/permissions/android.hardware.location.xml \
	frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
	frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
	frameworks/native/data/etc/android.hardware.wifi.direct.xml:system/etc/permissions/android.hardware.wifi.direct.xml \
	frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
	frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
	frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
	frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
	frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

# Packages
PRODUCT_PACKAGES := \
    lights.latona \
    sensors.latona \
    com.android.future.usb.accessory \
    bdaddr_read \
    uim-sysfs \
    libbt-vendor \
    bootmenu_busybox \
    SamsungServiceMode \
    DeviceParts

# Charger
PRODUCT_PACKAGES += \
    charger \
    charger_res_images

#Filesystem binaries
PRODUCT_PACKAGES += \
    bml_over_mtd \
    utility_make_ext4fs \
    make_ext4fs

# Support for Browser's saved page feature. This allows
# for pages saved on previous versions of the OS to be
# viewed on the current OS.
PRODUCT_PACKAGES += \
    libskia_legacy

# Audio
PRODUCT_PACKAGES += \
	libaudioutils audio.a2dp.default \
	audio.primary.omap3 audio.usb.default \
	audio.r_submix.default

PRODUCT_COPY_FILES += \
	device/samsung/galaxysl/libaudio/audio_policy.conf:system/etc/audio_policy.conf

#Camera
PRODUCT_PACKAGES += camera.latona

#Power
PRODUCT_PACKAGES += power.latona

# Script to edit the shipped nvs file to insert the device's assigned MAC
# address
PRODUCT_PACKAGES += store-mac-addr.sh

# WIFI
PRODUCT_PACKAGES += \
    hostapd \
    dhcpcd.conf \
    libwpa_client \
    wpa_supplicant \
    wpa_supplicant.conf

PRODUCT_PACKAGES += \
    wl127x-fw-4-sr.bin \
    wl127x-fw-4-mr.bin \
    wl127x-fw-4-plt.bin \
    wl1271-nvs_127x.bin

# device specific overlays
DEVICE_PACKAGE_OVERLAYS += device/samsung/galaxysl/overlay

# The OpenGL ES API level that is natively supported by this device.
# This is a 16.16 fixed point number
PRODUCT_PROPERTY_OVERRIDES := \
    ro.opengles.version=131072

# PowerVR graphics driver configuration
PRODUCT_COPY_FILES += \
   $(LOCAL_PATH)/etc/powervr.ini:system/etc/powervr.ini

# These are the hardware-specific settings that are stored in system properties.
# Note that the only such settings should be the ones that are too low-level to
# be reachable from resources or other mechanisms.
PRODUCT_PROPERTY_OVERRIDES += \
       wifi.interface=wlan0 \
       wifi.supplicant_scan_interval=180 \
       ro.telephony.ril_class=SamsungExynos3RIL \
       ro.telephony.ril.config=icccardstatus,datacall,signalstrength,facilitylock \
       ro.telephony.call_ring.multiple=false \
       ro.telephony.call_ring.delay=2000 \
       mobiledata.interfaces=pdp0,eth0,gprs,ppp0

# SGX530 is slower with the scissor optimization enabled
PRODUCT_PROPERTY_OVERRIDES += \
       ro.hwui.disable_scissor_opt=true

# GPU Producer to CPU Consumer
PRODUCT_PROPERTY_OVERRIDES += \
       ro.bq.gpu_to_cpu_unsupported=1

# Low RAM Device
PRODUCT_PROPERTY_OVERRIDES += \
       ro.config.low_ram=true \
       config.disable_atlas=true \
       config.disable_dock=true


# KSM
PRODUCT_PROPERTY_OVERRIDES += \
       ro.ksm.default=1

# enable Google-specific location features,
# like NetworkLocationProvider and LocationCollector
PRODUCT_PROPERTY_OVERRIDES += \
        ro.com.google.locationfeatures=1 \
        ro.com.google.networklocation=1

# Our cache partition isn't big enough for dalvik-cache.
PRODUCT_PROPERTY_OVERRIDES += \
        dalvik.vm.dexopt-data-only=1

# ART
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.dex2oat-Xms=8m \
    dalvik.vm.dex2oat-Xmx=96m \
    dalvik.vm.image-dex2oat-Xms=48m \
    dalvik.vm.image-dex2oat-Xmx=48m \
    dalvik.vm.dex2oat-filter=interpret-only \
    dalvik.vm.image-dex2oat-filter=speed

# ART
PRODUCT_DEX_PREOPT_DEFAULT_FLAGS := \
    --compiler-filter=interpret-only

# Override /proc/sys/vm/dirty_ratio on UMS
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vold.umsdirtyratio=20

# Vold
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    persist.sys.usb.config=mass_storage

PRODUCT_PROPERTY_OVERRIDES += \
	ro.vold.switchablepair=/storage/sdcard0,/storage/sdcard1
	ro.additionalmounts=/storage/sdcard1

include frameworks/native/build/phone-hdpi-512-dalvik-heap.mk

# Screen density is actually considered a locale (since it is taken into account
# the the build-time selection of resources). The product definitions including
# this file must pay attention to the fact that the first entry in the final
# PRODUCT_LOCALES expansion must not be a density.
PRODUCT_AAPT_CONFIG := normal hdpi

# copy the filesystem converter
PRODUCT_COPY_FILES += \
	device/samsung/galaxysl/updater.sh:updater.sh

# bml_over_mtd
PRODUCT_COPY_FILES += \
device/samsung/galaxysl/bml_over_mtd.sh:bml_over_mtd.sh

# See comment at the top of this file. This is where the other
# half of the device-specific product definition file takes care
# of the aspects that require proprietary drivers that aren't
# commonly available
$(call add-product-dex-preopt-module-config,services,--compiler-filter=speed)
$(call inherit-product, hardware/ti/omap3/omap3.mk)
$(call inherit-product, vendor/samsung/galaxysl/galaxysl-vendor.mk)
