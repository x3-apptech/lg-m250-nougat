# Copyright (C) 2009 The Android Open Source Project
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
#

LOCAL_PATH := $(call my-dir)

define add-libgpg-error-test-program
include $$(CLEAR_VARS)
LOCAL_MODULE := $(1)
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := $(2)
LOCAL_C_INCLUDES := \
    $$(LIBGPG_ERROR_BASE_DIR) \
    $$(LIBGPG_ERROR_BASE_DIR)/src \
    $$(empty)
LOCAL_CFLAGS := \
    -DHAVE_CONFIG_H \
    $$(empty)
LOCAL_SHARED_LIBRARIES := libgpg-error
include $$(BUILD_EXECUTABLE)

all-libgpg-error-targets: $$(LOCAL_MODULE)
clean-all-libgpg-error-targets: clean-$$(LOCAL_MODULE)
endef

$(eval $(call add-libgpg-error-test-program, \
    t-strerror, t-strerror.c))
$(eval $(call add-libgpg-error-test-program, \
    t-syserror, t-syserror.c))
