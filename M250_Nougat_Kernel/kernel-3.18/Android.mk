# Copyright (c) 2015 MediaTek Inc.
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

LOCAL_PATH := $(call my-dir)
KERNEL_ROOT_DIR := $(PWD)

define touch-kernel-image-timestamp
if [ -e $(1) ] && [ -e $(2) ] && cmp -s $(1) $(2); then \
 echo $(2) has no change;\
 mv -f $(1) $(2);\
else \
 rm -f $(1);\
fi
endef

define move-kernel-module-files
v=`cat $(2)/include/config/kernel.release`;\
for i in `grep -h '\.ko' /dev/null $(2)/.tmp_versions/*.mod`; do \
 o=`basename $$i`;\
 if [ -e $(1)/lib/modules/$$o ] && cmp -s $(1)/lib/modules/$$v/kernel/$$i $(1)/lib/modules/$$o; then \
  echo $(1)/lib/modules/$$o has no change;\
 else \
  echo Update $(1)/lib/modules/$$o;\
  mv -f $(1)/lib/modules/$$v/kernel/$$i $(1)/lib/modules/$$o;\
 fi;\
done
endef

define clean-kernel-module-dirs
rm -rf $(1)/lib/modules/$(if $(2),`cat $(2)/include/config/kernel.release`,*/)
endef

# '\\' in command is wrongly replaced to '\\\\' in kernel/out/arch/arm/boot/compressed/.piggy.xzkern.cmd
define fixup-kernel-cmd-file
if [ -e $(1) ]; then cp $(1) $(1).bak; sed -e 's/\\\\\\\\/\\\\/g' < $(1).bak > $(1); rm -f $(1).bak; fi
endef

ifeq ($(notdir $(LOCAL_PATH)),$(strip $(LINUX_KERNEL_VERSION)))
ifneq ($(strip $(TARGET_NO_KERNEL)),true)
    KERNEL_DIR := $(LOCAL_PATH)
    BUILT_SYSTEMIMAGE := $(call intermediates-dir-for,PACKAGING,systemimage)/system.img

  ifeq ($(KERNEL_CROSS_COMPILE),)
  ifeq ($(TARGET_ARCH), arm64)
    KERNEL_CROSS_COMPILE := $(KERNEL_ROOT_DIR)/$(TARGET_TOOLS_PREFIX)
  else
    KERNEL_CROSS_COMPILE := $(KERNEL_ROOT_DIR)/prebuilts/gcc/$(HOST_PREBUILT_TAG)/arm/arm-eabi-$(TARGET_GCC_VERSION)/bin/arm-eabi-
  endif
  endif
  ifeq ($(wildcard $(TARGET_PREBUILT_KERNEL)),)
    KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
    ifeq ($(TARGET_ARCH), arm64)
      ifeq ($(MTK_APPENDED_DTB_SUPPORT), yes)
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/Image.gz-dtb
      else
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/Image.gz
      endif
    else
      ifeq ($(MTK_APPENDED_DTB_SUPPORT), yes)
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/zImage-dtb
      else
        KERNEL_ZIMAGE_OUT := $(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/zImage
      endif
    endif
    ifeq ($(strip $(MTK_INTERNAL)),yes)
      KBUILD_BUILD_USER ?= mediatek
      KBUILD_BUILD_HOST ?= mediatek
    endif
    export KBUILD_BUILD_USER
    export KBUILD_BUILD_HOST
    export MTK_DTBO_FEATURE
    BUILT_KERNEL_TARGET := $(KERNEL_ZIMAGE_OUT).bin
    INSTALLED_KERNEL_TARGET := $(PRODUCT_OUT)/kernel
    TARGET_KERNEL_CONFIG := $(KERNEL_OUT)/.config
    KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
    KERNEL_HEADERS_TIMESTAMP := $(KERNEL_HEADERS_INSTALL)/build-timestamp
    KERNEL_CONFIG_FILE := $(KERNEL_DIR)/arch/$(TARGET_ARCH)/configs/$(KERNEL_DEFCONFIG)
#    KERNEL_CONFIG_MODULES := $(shell grep ^CONFIG_MODULES=y $(KERNEL_CONFIG_FILE))
    KERNEL_CONFIG_MODULES := yes
    KERNEL_MODULES_OUT := $(if $(filter /% ~%,$(TARGET_OUT)),,$(KERNEL_ROOT_DIR)/)$(TARGET_OUT)
    KERNEL_MODULES_DEPS := $(if $(wildcard $(KERNEL_MODULES_OUT)/lib/modules/*.ko),$(wildcard $(KERNEL_MODULES_OUT)/lib/modules/*.ko),$(KERNEL_MODULES_OUT)/lib/modules)
    KERNEL_MODULES_SYMBOLS_OUT := $(if $(filter /% ~%,$(TARGET_OUT_UNSTRIPPED)),,$(KERNEL_ROOT_DIR)/)$(TARGET_OUT_UNSTRIPPED)/system
    KERNEL_MAKE_OPTION := O=../$(KERNEL_OUT) ARCH=$(TARGET_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) ROOTDIR=$(KERNEL_ROOT_DIR) $(if $(strip $(SHOW_COMMANDS)),V=1)
ifneq (yes,$(filter $(MTK_BSP_PACKAGE) $(MTK_BASIC_PACKAGE),yes))
ifneq ($(strip $(MTK_EMMC_SUPPORT)),yes)
ifeq  ($(strip $(MTK_NAND_UBIFS_SUPPORT)),yes)
    KERNEL_MAKE_OPTION += LOCALVERSION=
endif
endif
endif

    # add LGE features
    KERNEL_MAKE_OPTION := $(KERNEL_MAKE_OPTION) \
      LGE_TARGET_PLATFORM=$(TARGET_BOARD_PLATFORM) \
      LGE_TARGET_DEVICE=$(TARGET_DEVICE) \

# .config cannot be PHONY due to config_data.gz
$(TARGET_KERNEL_CONFIG): $(KERNEL_CONFIG_FILE) $(LOCAL_PATH)/Android.mk
$(TARGET_KERNEL_CONFIG): $(shell find $(KERNEL_DIR) -name "Kconfig*") | $(KERNEL_OUT)
	echo "Generating a .config file for header generation"
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) $(KERNEL_DEFCONFIG)

$(KERNEL_MODULES_DEPS): $(KERNEL_ZIMAGE_OUT) ;

.KATI_RESTAT: $(KERNEL_ZIMAGE_OUT)
$(KERNEL_ZIMAGE_OUT): $(KERNEL_HEADERS_TIMESTAMP) FORCE | $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION)
	$(hide) $(call fixup-kernel-cmd-file,$(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/compressed/.piggy.xzkern.cmd)
ifeq ($(strip $(MTK_DTBO_FEATURE)), yes)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) dtboimage
endif
ifneq ($(KERNEL_CONFIG_MODULES),)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) modules
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) INSTALL_MOD_PATH=$(KERNEL_MODULES_SYMBOLS_OUT) modules_install
	$(hide) $(call move-kernel-module-files,$(KERNEL_MODULES_SYMBOLS_OUT),$(KERNEL_OUT))
	$(hide) $(call clean-kernel-module-dirs,$(KERNEL_MODULES_SYMBOLS_OUT),$(KERNEL_OUT))
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) INSTALL_MOD_PATH=$(KERNEL_MODULES_OUT) modules_install
	$(hide) $(call move-kernel-module-files,$(KERNEL_MODULES_OUT),$(KERNEL_OUT))
	$(hide) $(call clean-kernel-module-dirs,$(KERNEL_MODULES_OUT),$(KERNEL_OUT))
endif

ifeq ($(PRODUCT_SUPPORT_EXFAT), y)
	@cp -f $(ANDROID_BUILD_TOP)/kernel-3.18/tuxera_update.sh $(ANDROID_BUILD_TOP)
	@sh tuxera_update.sh --target target/lg.d/mobile-$(TARGET_BOARD_PLATFORM) --use-cache --latest --max-cache-entries 2 --source-dir $(ANDROID_BUILD_TOP)/kernel-3.18 --output-dir $(KERNEL_OUT) -a --user lg-mobile --pass AumlTsj0ou
	@tar -xzf tuxera-exfat*.tgz
	@mkdir -p $(TARGET_OUT_EXECUTABLES)
	@cp $(ANDROID_BUILD_TOP)/tuxera-exfat*/exfat/kernel-module/texfat.ko $(ANDROID_BUILD_TOP)/$(TARGET_OUT_EXECUTABLES)/../lib/modules/
	@cp $(ANDROID_BUILD_TOP)/tuxera-exfat*/exfat/tools/* $(TARGET_OUT_EXECUTABLES)
	#perl $(ANDROID_BUILD_TOP)/kernel-3.18/scripts/sign-file sha1 $(KERNEL_OUT)/signing_key.priv $(KERNEL_OUT)/signing_key.x509 $(KERNEL_MODULES_OUT)/lib/modules/texfat.ko
	@rm -f kheaders*.tar.bz2
	@rm -f tuxera-exfat*.tgz
	@rm -rf tuxera-exfat*
	@rm -f tuxera_update.sh
endif
ifeq ($(strip $(MTK_HEADER_SUPPORT)), yes)
$(BUILT_KERNEL_TARGET): $(KERNEL_ZIMAGE_OUT) $(LOCAL_PATH)/Android.mk $(KERNEL_HEADERS_INSTALL) | $(HOST_OUT_EXECUTABLES)/mkimage$(HOST_EXECUTABLE_SUFFIX)
	$(hide) $(HOST_OUT_EXECUTABLES)/mkimage$(HOST_EXECUTABLE_SUFFIX) $< KERNEL 0xffffffff > $@
else
$(BUILT_KERNEL_TARGET): $(KERNEL_ZIMAGE_OUT) $(LOCAL_PATH)/Android.mk $(KERNEL_HEADERS_INSTALL) | $(ACP)
	$(copy-file-to-target)
endif

$(TARGET_PREBUILT_KERNEL): $(BUILT_KERNEL_TARGET) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-new-target)

  else
    BUILT_KERNEL_TARGET := $(TARGET_PREBUILT_KERNEL)
  endif#TARGET_PREBUILT_KERNEL

ifeq ($(strip $(MTK_DTBO_FEATURE)), yes)
INSTALLED_DTB_OVERLAY_TARGET := $(PRODUCT_OUT)/dtbo.img
BUILT_DTB_OVERLAY_TARGET := $(KERNEL_OUT)/arch/$(TARGET_ARCH)/boot/dts/overlays/dtbo.img

$(BUILT_DTB_OVERLAY_TARGET): $(BUILT_KERNEL_TARGET)

$(INSTALLED_DTB_OVERLAY_TARGET): $(BUILT_DTB_OVERLAY_TARGET) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-target)
endif

$(INSTALLED_KERNEL_TARGET): $(BUILT_KERNEL_TARGET) $(INSTALLED_DTB_OVERLAY_TARGET) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-target)

ifneq ($(KERNEL_CONFIG_MODULES),)
$(BUILT_SYSTEMIMAGE): $(KERNEL_MODULES_DEPS)
endif

KERNEL_HEADER_DEFCONFIG := $(strip $(KERNEL_HEADER_DEFCONFIG))
ifeq ($(KERNEL_HEADER_DEFCONFIG),)
KERNEL_HEADER_DEFCONFIG := $(KERNEL_DEFCONFIG)
endif
KERNEL_CONFIG := $(TARGET_KERNEL_CONFIG)

.PHONY: kernel save-kernel kernel-savedefconfig %config-kernel clean-kernel dtboimage
kernel: $(INSTALLED_KERNEL_TARGET)
save-kernel: $(TARGET_PREBUILT_KERNEL)

kernel-savedefconfig: $(TARGET_KERNEL_CONFIG)
	cp $(TARGET_KERNEL_CONFIG) $(KERNEL_CONFIG_FILE)

kernel-menuconfig: | $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) menuconfig

%config-kernel: | $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) $(patsubst %config-kernel,%config,$@)

clean-kernel:
	$(hide) rm -rf $(KERNEL_OUT) $(KERNEL_MODULES_OUT) $(INSTALLED_KERNEL_TARGET)
ifeq ($(strip $(MTK_DTBO_FEATURE)), yes)
	$(hide) rm -f $(INSTALLED_DTB_OVERLAY_TARGET)

dtboimage: $(TARGET_KERNEL_CONFIG) FORCE
	$(hide) mkdir -p $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) dtboimage
	$(hide) cp $(BUILT_DTB_OVERLAY_TARGET) $(INSTALLED_DTB_OVERLAY_TARGET)
endif

$(KERNEL_OUT):
	$(hide) mkdir -p $@

$(KERNEL_HEADERS_TIMESTAMP) : $(KERNEL_HEADERS_INSTALL)
$(KERNEL_HEADERS_INSTALL) : $(TARGET_KERNEL_CONFIG) | $(KERNEL_OUT)
	$(hide) if [ ! -z "$(KERNEL_HEADER_DEFCONFIG)" ]; then \
				rm -f ../$(KERNEL_CONFIG); \
				$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) headers_install; fi
	$(hide) if [ "$(KERNEL_HEADER_DEFCONFIG)" != "$(KERNEL_DEFCONFIG)" ]; then \
				echo "Used a different defconfig for header generation"; \
				rm -f ../$(KERNEL_CONFIG); \
				$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) $(KERNEL_DEFCONFIG); fi
	$(hide) if [ ! -z "$(KERNEL_CONFIG_OVERRIDE)" ]; then \
				echo "Overriding kernel config with '$(KERNEL_CONFIG_OVERRIDE)'"; \
				echo $(KERNEL_CONFIG_OVERRIDE) >> $(TARGET_KERNEL_CONFIG); \
				$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) oldconfig; fi
	$(hide) touch $@/build-timestamp


.PHONY: check-kernel-config check-kernel-dotconfig
droid: check-kernel-config check-kernel-dotconfig
check-mtk-config: check-kernel-config check-kernel-dotconfig
check-kernel-config: $(TARGET_KERNEL_CONFIG)
ifneq (yes,$(strip $(DISABLE_MTK_CONFIG_CHECK)))
	python device/mediatek/build/build/tools/check_kernel_config.py -c $(MTK_TARGET_PROJECT_FOLDER)/ProjectConfig.mk -k $(TARGET_KERNEL_CONFIG) -p $(MTK_PROJECT_NAME)
else
	-python device/mediatek/build/build/tools/check_kernel_config.py -c $(MTK_TARGET_PROJECT_FOLDER)/ProjectConfig.mk -k $(TARGET_KERNEL_CONFIG) -p $(MTK_PROJECT_NAME)
endif


ifneq ($(filter check-mtk-config check-kernel-dotconfig,$(MAKECMDGOALS)),)
.PHONY: $(TARGET_KERNEL_CONFIG)
endif
check-kernel-dotconfig: $(TARGET_KERNEL_CONFIG)
ifneq (yes,$(strip $(DISABLE_MTK_CONFIG_CHECK)))
	python device/mediatek/build/build/tools/check_kernel_config.py -c $(MTK_TARGET_PROJECT_FOLDER)/ProjectConfig.mk -k $(TARGET_KERNEL_CONFIG) -p $(MTK_PROJECT_NAME)
else
	-python device/mediatek/build/build/tools/check_kernel_config.py -c $(MTK_TARGET_PROJECT_FOLDER)/ProjectConfig.mk -k $(TARGET_KERNEL_CONFIG) -p $(MTK_PROJECT_NAME)
endif


endif#TARGET_NO_KERNEL
endif#LINUX_KERNEL_VERSION
