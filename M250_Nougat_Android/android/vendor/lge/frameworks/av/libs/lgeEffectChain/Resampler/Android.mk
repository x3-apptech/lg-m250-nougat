# build to Shared Lib

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

$(warning BGMMixResampler Build)

ifeq ($(TARGET_ARCH),arm64)
LOCAL_MULTILIB := 32
endif

#LOCAL_SRC_FILES:= src/filterkit.c \
#       src/resamplesubs.c \
#       src/windowfilter.c

LOCAL_SRC_FILES:= src/ResampleClass.cpp

LOCAL_C_INCLUDES += $(TOPDIR)vendor/lge/frameworks/av/libs/lgeEffectChain/Resampler/inc \
LOCAL_C_INCLUDES += $(TOPDIR)vendor/lge/frameworks/av/libs/lgeEffectChain/Resampler/interface \
LOCAL_C_INCLUDES += $(TOPDIR)vendor/lge/frameworks/av/libs/lgeEffectChain/EffectChain/inc


LOCAL_MODULE:= liblge_effectchainresampler
LOCAL_MODULE_TAGS := optional eng

LOCAL_CFLAGS += -O3

LOCAL_SHARED_LIBRARIES:= liblog \
                libcutils \
                libutils \

include $(BUILD_SHARED_LIBRARY)
