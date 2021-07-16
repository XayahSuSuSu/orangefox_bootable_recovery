#
#	This file is part of the OrangeFox Recovery Project
# 	Copyright (C) 2018-2021 The OrangeFox Recovery Project
#	
#	OrangeFox is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	any later version.
#
#	OrangeFox is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
# 	This software is released under GPL version 3 or any later version.
#	See <http://www.gnu.org/licenses/>.
# 	
# 	Please maintain this if you use this script or any part of it
#

#LOCAL_CFLAGS += -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough
#LOCAL_CFLAGS += -Wno-format-extra-args

ifneq ($(FOX_VERSION),)
    LOCAL_CFLAGS += -DFOX_VERSION='"$(FOX_VERSION)"'
else
    LOCAL_CFLAGS += -DFOX_VERSION='"Unofficial"'
endif

ifeq ($(FOX_DEVICE_MODEL),)
    DEVICE := $(subst omni_,,$(TARGET_PRODUCT))
    LOCAL_CFLAGS += -DFOX_DEVICE_MODEL='"$(DEVICE)"'
endif

ifeq ($(OF_VANILLA_BUILD),1)
    LOCAL_CFLAGS += -DOF_VANILLA_BUILD='"1"'
    OF_SKIP_ORANGEFOX_PROCESS := 1
    OF_DISABLE_MIUI_SPECIFIC_FEATURES := 1
    OF_DONT_PATCH_ENCRYPTED_DEVICE := 1
    OF_DONT_PATCH_ON_FRESH_INSTALLATION := 1
    OF_KEEP_DM_VERITY_FORCED_ENCRYPTION := 1
    OF_TWRP_COMPATIBILITY_MODE := 1
endif

ifeq ($(OF_DISABLE_MIUI_SPECIFIC_FEATURES),1)
    LOCAL_CFLAGS += -DOF_DISABLE_MIUI_SPECIFIC_FEATURES='"1"'
    OF_TWRP_COMPATIBILITY_MODE := 1
endif

ifeq ($(OF_DISABLE_MIUI_OTA_BY_DEFAULT),1)
    LOCAL_CFLAGS += -DOF_DISABLE_MIUI_OTA_BY_DEFAULT='"1"'
endif

ifeq ($(OF_TWRP_COMPATIBILITY_MODE),1)
    LOCAL_CFLAGS += -DOF_TWRP_COMPATIBILITY_MODE='"1"'
    OF_DISABLE_MIUI_SPECIFIC_FEATURES := 1
endif

ifeq ($(OF_SKIP_ORANGEFOX_PROCESS),1)
    LOCAL_CFLAGS += -DOF_SKIP_ORANGEFOX_PROCESS='"1"'
    OF_DONT_PATCH_ON_FRESH_INSTALLATION := 1
endif

ifeq ($(OF_DONT_PATCH_ON_FRESH_INSTALLATION),1)
    LOCAL_CFLAGS += -DOF_DONT_PATCH_ON_FRESH_INSTALLATION='"1"'
endif

ifneq ($(FOX_BUILD_TYPE),)
    LOCAL_CFLAGS += -DFOX_BUILD_TYPE='"$(FOX_BUILD_TYPE)"'
else
    LOCAL_CFLAGS += -DFOX_BUILD_TYPE='"Unofficial"'
endif

ifeq ($(OF_USE_MAGISKBOOT),1)
    LOCAL_CFLAGS += -DOF_USE_MAGISKBOOT='"1"'
endif

ifeq ($(OF_USE_MAGISKBOOT_FOR_ALL_PATCHES),1)
    LOCAL_CFLAGS += -DOF_USE_MAGISKBOOT_FOR_ALL_PATCHES='"1"'
    LOCAL_CFLAGS += -DOF_USE_MAGISKBOOT='"1"'
endif

ifeq ($(OF_FORCE_MAGISKBOOT_BOOT_PATCH_MIUI),1)
    LOCAL_CFLAGS += -DOF_FORCE_MAGISKBOOT_BOOT_PATCH_MIUI='"1"'
endif

ifeq ($(OF_NO_MIUI_PATCH_WARNING),1)
    LOCAL_CFLAGS += -DOF_NO_MIUI_PATCH_WARNING='"1"'
endif

ifeq ($(OF_AB_DEVICE),1)
    LOCAL_CFLAGS += -DOF_AB_DEVICE='"1"'
    LOCAL_CFLAGS += -DOF_USE_MAGISKBOOT_FOR_ALL_PATCHES='"1"'
    LOCAL_CFLAGS += -DOF_USE_MAGISKBOOT='"1"'
    ifneq ($(AB_OTA_UPDATER),true)
    	LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
    	LOCAL_SHARED_LIBRARIES += libhardware android.hardware.boot@1.0
    	TWRP_REQUIRED_MODULES += libhardware android.hardware.boot@1.0-service android.hardware.boot@1.0-service.rc
    endif
endif

ifeq ($(OF_DONT_PATCH_ENCRYPTED_DEVICE),1)
    LOCAL_CFLAGS += -DOF_DONT_PATCH_ENCRYPTED_DEVICE='"1"'
endif

ifneq ($(OF_MAINTAINER),)
    LOCAL_CFLAGS += -DOF_MAINTAINER='"$(OF_MAINTAINER)"'
else
    LOCAL_CFLAGS += -DOF_MAINTAINER='"Testing build (unofficial)"'
endif

ifneq ($(OF_FLASHLIGHT_ENABLE),)
    LOCAL_CFLAGS += -DOF_FLASHLIGHT_ENABLE='"$(OF_FLASHLIGHT_ENABLE)"'
else
    LOCAL_CFLAGS += -DOF_FLASHLIGHT_ENABLE='"1"'
endif

ifneq ($(OF_SPLASH_MAX_SIZE),)
    LOCAL_CFLAGS += -DOF_SPLASH_MAX_SIZE='"$(OF_SPLASH_MAX_SIZE)"'
else
    LOCAL_CFLAGS += -DOF_SPLASH_MAX_SIZE='"4096"'
endif

ifneq ($(FOX_ADVANCED_SECURITY),)
    LOCAL_CFLAGS += -DFOX_ADVANCED_SECURITY='"$(FOX_ADVANCED_SECURITY)"'
endif

ifneq ($(FOX_CURRENT_DEV_STR),)
    LOCAL_CFLAGS += -DFOX_CURRENT_DEV_STR='"$(FOX_CURRENT_DEV_STR)"'
else
    LOCAL_CFLAGS += -DFOX_CURRENT_DEV_STR='"latest"'
endif

ifeq ($(FOX_OLD_DECRYPT_RELOAD),1)
    LOCAL_CFLAGS += -DFOX_OLD_DECRYPT_RELOAD='"1"'
endif

ifneq ($(OF_SCREEN_H),)
    LOCAL_CFLAGS += -DOF_SCREEN_H='"$(OF_SCREEN_H)"'
else
    LOCAL_CFLAGS += -DOF_SCREEN_H='"1920"'
endif

ifneq ($(OF_STATUS_H),)
    LOCAL_CFLAGS += -DOF_STATUS_H='"$(OF_STATUS_H)"'
else
    LOCAL_CFLAGS += -DOF_STATUS_H='"72"'
endif

ifneq ($(OF_HIDE_NOTCH),)
    LOCAL_CFLAGS += -DOF_HIDE_NOTCH='"$(OF_HIDE_NOTCH)"'
else
    LOCAL_CFLAGS += -DOF_HIDE_NOTCH='"0"'
endif

ifneq ($(OF_STATUS_INDENT_LEFT),)
    LOCAL_CFLAGS += -DOF_STATUS_INDENT_LEFT='"$(OF_STATUS_INDENT_LEFT)"'
else
    LOCAL_CFLAGS += -DOF_STATUS_INDENT_LEFT='"20"'
endif

ifneq ($(OF_STATUS_INDENT_RIGHT),)
    LOCAL_CFLAGS += -DOF_STATUS_INDENT_RIGHT='"$(OF_STATUS_INDENT_RIGHT)"'
else
    LOCAL_CFLAGS += -DOF_STATUS_INDENT_RIGHT='"20"'
endif

ifneq ($(OF_CLOCK_POS),)
    LOCAL_CFLAGS += -DOF_CLOCK_POS='"$(OF_CLOCK_POS)"'
else
    LOCAL_CFLAGS += -DOF_CLOCK_POS='"0"'
endif

ifneq ($(OF_ALLOW_DISABLE_NAVBAR),)
    LOCAL_CFLAGS += -DOF_ALLOW_DISABLE_NAVBAR='"$(OF_ALLOW_DISABLE_NAVBAR)"'
else
    LOCAL_CFLAGS += -DOF_ALLOW_DISABLE_NAVBAR='"1"'
endif

ifneq ($(OF_FL_PATH1),)
    LOCAL_CFLAGS += -DOF_FL_PATH1='"$(OF_FL_PATH1)"'
else
    LOCAL_CFLAGS += -DOF_FL_PATH1='""'
endif

ifneq ($(OF_FL_PATH2),)
    LOCAL_CFLAGS += -DOF_FL_PATH2='"$(OF_FL_PATH2)"'
else
    LOCAL_CFLAGS += -DOF_FL_PATH2='""'
endif

ifeq ($(OF_USE_HEXDUMP),1)
    LOCAL_CFLAGS += -DOF_USE_HEXDUMP='"1"'
endif

ifeq ($(OF_SKIP_FBE_DECRYPTION),1)
    LOCAL_CFLAGS += -DOF_SKIP_FBE_DECRYPTION='"1"'
endif

ifeq ($(OF_CLASSIC_LEDS_FUNCTION),1)
    LOCAL_CFLAGS += -DOF_CLASSIC_LEDS_FUNCTION='"1"'
endif

ifeq ($(OF_SUPPORT_PRE_FLASH_SCRIPT),1)
    LOCAL_CFLAGS += -DOF_SUPPORT_PRE_FLASH_SCRIPT='"1"'
endif

ifneq ($(TW_OZIP_DECRYPT_KEY),)
    OF_SUPPORT_OZIP_DECRYPTION := 1
endif

ifeq ($(OF_SUPPORT_OZIP_DECRYPTION),1)
    LOCAL_CFLAGS += -DOF_SUPPORT_OZIP_DECRYPTION='"1"'
    RECOVERY_BINARY_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/ozip_decrypt
endif

ifeq ($(OF_KEEP_DM_VERITY_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_KEEP_DM_VERITY_FORCED_ENCRYPTION='"1"'
    OF_KEEP_DM_VERITY := 1
    OF_KEEP_FORCED_ENCRYPTION := 1
endif

ifneq ($(OF_TARGET_DEVICES),)
    LOCAL_CFLAGS += -DOF_TARGET_DEVICES='"$(OF_TARGET_DEVICES)"'
endif

ifeq ($(OF_KEEP_DM_VERITY),1)
    LOCAL_CFLAGS += -DOF_KEEP_DM_VERITY='"1"'
endif

ifeq ($(OF_KEEP_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_KEEP_FORCED_ENCRYPTION='"1"'
endif

ifeq ($(OF_DISABLE_DM_VERITY_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_DISABLE_DM_VERITY_FORCED_ENCRYPTION='"1"'
    OF_DISABLE_DM_VERITY := 1
    OF_DISABLE_FORCED_ENCRYPTION := 1
endif

ifeq ($(OF_DISABLE_DM_VERITY),1)
    LOCAL_CFLAGS += -DOF_DISABLE_DM_VERITY='"1"'
endif

ifeq ($(OF_DISABLE_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_DISABLE_FORCED_ENCRYPTION='"1"'
endif

ifeq ($(OF_FORCE_DISABLE_DM_VERITY_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_FORCE_DISABLE_DM_VERITY_FORCED_ENCRYPTION='"1"'
    OF_FORCE_DISABLE_DM_VERITY := 1
    OF_FORCE_DISABLE_FORCED_ENCRYPTION := 1
endif

ifeq ($(OF_FORCE_DISABLE_DM_VERITY),1)
    LOCAL_CFLAGS += -DOF_FORCE_DISABLE_DM_VERITY='"1"'
endif

ifeq ($(OF_FORCE_DISABLE_FORCED_ENCRYPTION),1)
    LOCAL_CFLAGS += -DOF_FORCE_DISABLE_FORCED_ENCRYPTION='"1"'
endif

ifeq ($(OF_CHECK_OVERWRITE_ATTEMPTS),1)
    LOCAL_CFLAGS += -DOF_CHECK_OVERWRITE_ATTEMPTS='"1"'
endif

ifeq ($(FOX_ENABLE_LAB),1)
    LOCAL_CFLAGS += -DFOX_ENABLE_LAB='"1"'
endif

ifeq ($(FOX_USE_NANO_EDITOR), 1)
    LOCAL_CFLAGS += -DFOX_USE_NANO_EDITOR='"1"'
endif

ifeq ($(OF_NO_MIUI_OTA_VENDOR_BACKUP),1)
    LOCAL_CFLAGS += -DOF_NO_MIUI_OTA_VENDOR_BACKUP='"1"'
endif

ifeq ($(OF_REDUCE_DECRYPTION_TIMEOUT),1)
    LOCAL_CFLAGS += -DOF_REDUCE_DECRYPTION_TIMEOUT='"1"'
endif

ifeq ($(OF_DONT_KEEP_LOG_HISTORY),1)
    LOCAL_CFLAGS += -DOF_DONT_KEEP_LOG_HISTORY='"1"'
endif

ifeq ($(OF_SUPPORT_ALL_BLOCK_OTA_UPDATES),1)
    LOCAL_CFLAGS += -DOF_SUPPORT_ALL_BLOCK_OTA_UPDATES='"1"'
endif

ifeq ($(OF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR),1)
    LOCAL_CFLAGS += -DOF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR='"1"'
endif

ifeq ($(OF_OTA_BACKUP_STOCK_BOOT_IMAGE),1)
    LOCAL_CFLAGS += -DOF_OTA_BACKUP_STOCK_BOOT_IMAGE
endif

ifeq ($(OF_FBE_METADATA_MOUNT_IGNORE),1)
    LOCAL_CFLAGS += -DOF_FBE_METADATA_MOUNT_IGNORE='"1"'
endif

ifeq ($(OF_PATCH_AVB20),1)
    LOCAL_CFLAGS += -DOF_PATCH_AVB20='"1"'
endif

ifeq ($(OF_SKIP_MULTIUSER_FOLDERS_BACKUP),1)
    LOCAL_CFLAGS += -DOF_SKIP_MULTIUSER_FOLDERS_BACKUP='"1"'
endif

ifneq ($(OF_QUICK_BACKUP_LIST),)
    LOCAL_CFLAGS += -DOF_QUICK_BACKUP_LIST='"$(OF_QUICK_BACKUP_LIST)"'
endif

ifeq ($(OF_USE_LOCKSCREEN_BUTTON),1)
    LOCAL_CFLAGS += -DOF_USE_LOCKSCREEN_BUTTON
endif

ifeq ($(FOX_USE_LZMA_COMPRESSION),1)
    ifeq ($(LZMA_RAMDISK_TARGETS),)
    LZMA_RAMDISK_TARGETS := recovery
    endif
endif

ifeq ($(OF_NO_TREBLE_COMPATIBILITY_CHECK),1)
    LOCAL_CFLAGS += -DOF_NO_TREBLE_COMPATIBILITY_CHECK='"1"'
endif

ifeq ($(OF_INCREMENTAL_OTA_BACKUP_SUPER),1)
    LOCAL_CFLAGS += -DOF_INCREMENTAL_OTA_BACKUP_SUPER='"1"'
endif

ifeq ($(OF_REPORT_HARMLESS_MOUNT_ISSUES),1)
    LOCAL_CFLAGS += -DOF_REPORT_HARMLESS_MOUNT_ISSUES='"1"'
endif

ifeq ($(OF_OTA_RES_CHECK_MICROSD),1)
    LOCAL_CFLAGS += -DOF_OTA_RES_CHECK_MICROSD='"1"'
endif

# from gui/Android.mk
ifeq ($(FOX_ENABLE_LAB),1)
    LOCAL_CFLAGS += -DFOX_ENABLE_LAB='"1"'
endif
#

# samsung dynamic issues
ifeq ($(FOX_DYNAMIC_SAMSUNG_FIX),1)
    FOX_BUILD_BASH := 0
    FOX_EXCLUDE_NANO_EDITOR := 1
endif

# samsung haptics
ifeq ($(OF_USE_SAMSUNG_HAPTICS),1)
    TW_USE_SAMSUNG_HAPTICS := true
endif

# nano
ifeq ($(FOX_EXCLUDE_NANO_EDITOR),1)
    TW_EXCLUDE_NANO := true
endif

ifeq ($(FOX_USE_NANO_EDITOR),1)
    TW_EXCLUDE_NANO := true
endif

ifneq ($(TW_EXCLUDE_NANO), true)
    ifeq ($(wildcard external/nano/Android.mk),)
        $(warning Nano sources not found! You need to clone the sources.)
        $(warning Please run: "git clone --depth=1 https://github.com/LineageOS/android_external_nano -b lineage-17.1 external/nano")
        $(error Nano sources not present; exiting)
    endif
endif

# bash
ifeq ($(FOX_BUILD_BASH),1)
  ifeq ($(wildcard external/bash/Android.mk),)
        $(warning Bash sources not found! You need to clone the sources.)
        $(warning Please run: "git clone --depth=1 https://github.com/LineageOS/android_external_bash -b lineage-17.1 external/bash")
        $(error Bash sources not present; exiting)
  endif
  RECOVERY_BINARY_SOURCE_FILES += $(TARGET_OUT_OPTIONAL_EXECUTABLES)/bash
  TWRP_REQUIRED_MODULES += bash

  TWRP_REQUIRED_MODULES += \
    bash_fox
endif

# check for conflicts
ifeq ($(OF_SUPPORT_ALL_BLOCK_OTA_UPDATES),1)
   ifeq ($(OF_DISABLE_MIUI_SPECIFIC_FEATURES),1)
        $(warning You cannot use "OF_SUPPORT_ALL_BLOCK_OTA_UPDATES" with "OF_DISABLE_MIUI_SPECIFIC_FEATURES"/"OF_TWRP_COMPATIBILITY_MODE")
        $(error Fix your build vars!; exiting)
   endif
endif

# disable by default the USB storage button on the "Mount" menu
ifneq ($(OF_ENABLE_USB_STORAGE),1)
    TW_NO_USB_STORAGE := true
endif

# post-format
ifeq ($(OF_RUN_POST_FORMAT_PROCESS),1)
    LOCAL_CFLAGS += -DOF_RUN_POST_FORMAT_PROCESS='"1"'
endif

# turn some errors in mounting logical partitions into log entries only
ifeq ($(OF_IGNORE_LOGICAL_MOUNT_ERRORS),1)
    LOCAL_CFLAGS += -DOF_IGNORE_LOGICAL_MOUNT_ERRORS='"1"'
endif
#

# process these here instead of OrangeFox.sh
ifeq ($(FOX_ENABLE_APP_MANAGER),1)
    LOCAL_CFLAGS += -DFOX_ENABLE_APP_MANAGER='"1"'
endif

ifeq ($(OF_DISABLE_EXTRA_ABOUT_PAGE),1)
    LOCAL_CFLAGS += -DOF_DISABLE_EXTRA_ABOUT_PAGE='"1"'
endif

ifeq ($(OF_NO_SPLASH_CHANGE),1)
    LOCAL_CFLAGS += -DOF_NO_SPLASH_CHANGE='"1"'
endif

ifeq ($(FOX_DELETE_MAGISK_ADDON),1)
    LOCAL_CFLAGS += -DFOX_DELETE_MAGISK_ADDON='"1"'
endif

ifeq ($(OF_USE_GREEN_LED),0)
    LOCAL_CFLAGS += -DOF_USE_GREEN_LED='"0"'
endif
#
