LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    keyutils.c \
    $(empty)


LOCAL_CFLAGS := \
    $(empty)

LOCAL_MODULE:= libkeyutils

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_CLASS := SHARED_LIBRARIES
include $(BUILD_SHARED_LIBRARY)


## keyctl ##
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    keyctl.c \
    $(empty)

LOCAL_SHARED_LIBRARIES := \
    libkeyutils  

LOCAL_CFLAGS := \
    $(empty)

LOCAL_MODULE:= keyctl 
include $(BUILD_EXECUTABLE)


## request-key ##
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    request-key.c \
    $(empty)

LOCAL_SHARED_LIBRARIES := \
    libkeyutils  

LOCAL_CFLAGS := \
    $(empty)

LOCAL_MODULE:= request-key
include $(BUILD_EXECUTABLE)
