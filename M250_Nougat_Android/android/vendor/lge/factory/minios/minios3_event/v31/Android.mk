
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

include $(LOCAL_PATH)/../../minios3_config/minios3_config.mk

ifeq ($(MINIOS_AAT_VERSION), 31)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../minios3_include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../minios3_displayinfo
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../minios3_device/input
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include \
                    $(QC_PROP_ROOT)/common/inc \
                    system/core/include/cutils

LOCAL_SRC_FILES := \
    active.c \
    error.c \
    events.c \
    expose.c \
    fb_events.c \
    fb_input.c \
    fb_mouse.c \
    fb_sensor.c \
    keyboard.c \
    mouse.c \
    quit.c \
    string.c \
    sysmutex.c \
    syssem.c \
    systhread.c \
    systimer.c \
    thread.c \
    timer.c

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    liblog \
    libbinder \
    libminios3_sensor_proxy \
    libminios3_displayinfo \
    libminios3_interface \
    libminios3_input_proxy

LOCAL_STATIC_LIBRARIES :=

#LOCAL_SHARED_LIBRARIES += libskia
#LOCAL_C_INCLUDES += external/skia/include/core
#LOCAL_C_INCLUDES += external/skia/include/images

#LOCAL_CFLAGS += -fpermissive
LOCAL_CFLAGS += -DFBCON_DEBUG
LOCAL_CFLAGS += -DDEBUG_VIDEO
LOCAL_CFLAGS += -DDEBUG_PALETTE
LOCAL_CFLAGS += -DDEBUG_THREADS

LOCAL_MODULE := libminios3_event
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := lge
ifeq (1,$(SUPPORT_64BIT))
LOCAL_MULTILIB := both
LOCAL_MODULE_PATH_32 := $(TARGET_FACTORY_RAMDISK_OUT)/vendor/factory_lib
LOCAL_MODULE_PATH_64 := $(TARGET_FACTORY_RAMDISK_OUT)/vendor/factory_lib64
else
LOCAL_MODULE_PATH := $(TARGET_FACTORY_RAMDISK_OUT)/vendor/factory_lib
endif


include $(BUILD_SHARED_LIBRARY)
endif
