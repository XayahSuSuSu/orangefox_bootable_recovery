/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _VARIABLES_HEADER_
#define _VARIABLES_HEADER_

#define TW_MAIN_VERSION_STR       "3.2.3"
#define TW_VERSION_STR TW_MAIN_VERSION_STR TW_DEVICE_VERSION

// OrangeFox - Values
#define RW_BUILD                TW_DEVICE_VERSION
#define RW_DEVICE               RW_DEVICE_MODEL
#define RW_VERSION              TW_MAIN_VERSION_STR
#define OF_MAINTAINER_STR      	"of_maintainer"
#define OF_FLASHLIGHT_ENABLE_STR "of_flashlight_enable"

// *** OrangeFox - Variables ** //
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
static const std::string Fox_Tmp = "/tmp";
static const std::string Fox_Home = "/sdcard/Fox";
static const std::string Fox_Home_Files = Fox_Home + "/FoxFiles";
static const std::string Fox_sdcard_aroma_cfg = Fox_Home + "/aromafm.cfg";
static const std::string Fox_Themes_Dir = "/Fox/theme";
static const std::string FFiles_dir = "/FFiles";
static const std::string Fox_aroma_cfg = FFiles_dir + "/AromaFM/AromaFM.zip.cfg";
static const std::string Fox_tmp_dir = Fox_Tmp + "/orangefox";
static const std::string Fox_ramdisk_dir = Fox_tmp_dir + "/ramdisk"; 
static const std::string Fox_ramdisk_sbin_dir = Fox_ramdisk_dir + "/sbin"; 
static const std::string epoch_drift_file = "/persist/.fox_epoch_drift.cfg"; // to cater for any saved epoch_drifts
static const std::string Fox_OTA_info = "/orangefox.info";
static std::string Fox_Current_Device = "mido";

static int Fox_Zip_Installer_Code = 0; // 0=standard zip;1=custom ROM;2=miui ROM; 11=custom treble ROM; 22=miui treble ROM
static int Fox_IsDeactivation_Process_Called = 0; // have we called the deactivation process
static int Fox_AutoDeactivate_OnReboot = 0;   // call the deactivation process automatically on reboot (if not already called by another thread) ?
static int Fox_Force_Deactivate_Process = 0;  // for a call to Deactivate_Process()
static int Fox_Current_ROM_IsTreble = 0; // is the currently installed ROM a treble ROM?
static int Fox_Current_ROM_IsMIUI = 0; // is the currently installed ROM a MIUI ROM?

#define RW_SURVIVAL_FOLDER    Fox_Home.c_str()
//#define FOX_UPDATE_BINARY  "META-INF/com/google/android/update-binary" // all zip installers must have this
#define FOX_MIUI_UPDATE_PATH "META-INF/com/miui/miui_update" 	// standard MIUI ROMs have this
#define FOX_FORCE_DEACTIVATE_PROCESS "fox_force_deactivate_process"
#define FOX_ZIP_INSTALLER_CODE "fox_zip_installer_code"

// **** //
#define RW_SURVIVAL_FOLDER_VAR      "fox_survival_backup_folder_path"
#define RW_SURVIVAL_BACKUP_NAME       "fox_survival_backup_folder_name"
#define RW_SURVIVAL_BACKUP       "OTA"
#define RW_FILES_BACKUPS_FOLDER_VAR       "rw_files_backup_folder_var"
#define RW_DISABLE_BOOT_CHK       "fox_disable_boot_check"
#define RW_DO_SYSTEM_ON_OTA       "fox_include_system_survival"
#define RW_PASSWORD_VARIABLE       "heslo"
#define RW_INSTALL_PREBUILT_ZIP       "fox_install_built_in_zip"
#define RW_DONT_REPLACE_STOCK       "fox_reboot_dont_disable_stock_recovery"
#define RW_ACTUAL_BUILD_VAR              "rw_actual_build"
#define RW_INCREMENTAL_PACKAGE          "fox_support_miui_ota"
#define RW_ENABLE_SECURE_RO             "fox_reboot_enable_secure_ro"
#define RW_DISABLE_SECURE_RO             "fox_reboot_disable_secure_ro"
#define RW_ENABLE_ADB_RO             "fox_reboot_enable_adb_ro"
#define RW_DISABLE_ADB_RO             "fox_reboot_disable_adb_ro"
#define RW_ADVANCED_WARN_CHK             "fox_advanced_warning_checkbox"
#define RW_DISABLE_MOCK_LOCATION           "fox_reboot_disable_mock_location"
#define RW_ENABLE_MOCK_LOCATION           "fox_reboot_enable_mock_location"
#define RW_DISABLE_SECURE_BOOT           "fox_reboot_disable_secure_boot"
#define RW_ADVANCED_STOCK_REPLACE           "fox_reboot_advanced_stock_recovery_check"
#define RW_SAVE_LOAD_AROMAFM           "fox_reboot_saveload_aromafm_check"
#define RW_DISABLE_DEBUGGING           "fox_reboot_disable_debugging_check"
#define RW_ENABLE_DEBUGGING           "fox_reboot_forced_debugging_check"
#define RW_DISABLE_FORCED_ENCRYPTION           "fox_reboot_forced_encryption_check"
#define RW_DISABLE_DM_VERITY           "fox_reboot_dm_verity_check"
#define RW_REBOOT_AFTER_RESTORE           "rw_reboot_after_restore"
#define RW_COMPATIBILITY_DEVICE         "fox_compatibility_fox_device"
#define RW_MAIN_SURVIVAL_TRIGGER         "fox_main_survival_trigger"
//#define RW_SUPERSU_CONFIG           "rw_supersu_config_chk"
#define RW_NO_OS_SEARCH_ENGINE           "fox_noos_engine"
#define RW_STUPID_COOKIE_STUFF           "rw_game_bobs_max"
#define RW_TMP_SCRIPT_DIR       "fox_tmp_script_directory"
#define RW_STATUSBAR_ON_LOCK       "fox_statusbar_on_lockpass"
#define RW_INSTALL_VIBRATE       "fox_data_install_vibrate"
#define RW_BACKUP_VIBRATE       "fox_data_backup_vibrate"
#define RW_RESTORE_VIBRATE       "fox_data_restore_vibrate"
#define RW_RESTORE_BLUE_LED       "fox_set_custom_led_restore_blue"
#define RW_RESTORE_RED_LED       "fox_set_custom_led_restore_red"
#define RW_RESTORE_GREEN_LED       "fox_set_custom_led_restore_green"
#define RW_BACKUP_RED_LED       "fox_set_custom_led_backup_red"
#define RW_BACKUP_GREEN_LED       "fox_set_custom_led_backup_green"
#define RW_BACKUP_BLUE_LED       "fox_set_custom_led_backup_blue"
#define RW_INSTALL_RED_LED       "fox_set_custom_led_install_red"
#define RW_INSTALL_GREEN_LED       "fox_set_custom_led_install_green"
#define RW_INSTALL_BLUE_LED       "fox_set_custom_led_install_blue"
#define RW_INSTALL_LED_COLOR       "fox_install_led_color"
#define RW_BACKUP_LED_COLOR       "fox_backup_led_color"
#define RW_RESTORE_LED_COLOR       "fox_res_led_color"
#define RW_BALANCE_CHECK       "fox_boot_balance_check"
#define RW_NOTIFY_AFTER_RESTORE       "rw_inject_after_restore"
#define RW_NOTIFY_AFTER_BACKUP       "rw_inject_after_backup"
#define RW_NOTIFY_AFTER_INSTALL      "rw_inject_after_zip"
#define RW_FLASHLIGHT_VAR     "flashlight"
#define RW_FSYNC_CHECK       "fox_boot_fsync_check"
#define RW_FORCE_FAST_CHARGE_CHECK       "fox_boot_fastcharge_check"
#define RW_T2W_CHECK       "fox_boot_t2w_check"
#define RW_PERFORMANCE_CHECK       "fox_boot_performance_check"
#define RW_POWERSAVE_CHECK       "fox_boot_powersave_check"
#define RW_CALL_DEACTIVATION         "fox_call_deactivation_process"
#define RW_GOVERNOR_STABLE         "governor_stable"

#define RW_MIUI_ZIP_TMP                    "fox_miui_zip_tmp"
#define RW_LOADED_FINGERPRINT                    "fox_loaded_singature"
#define RW_MIN_EXPECTED_FP_SIZE 30

#define RW_INCREMENTAL_OTA_FAIL                 "fox_ota_fail"
#define RW_RUN_SURVIVAL_BACKUP                 "fox_run_survival_backup"
#define RW_METADATA_PRE_BUILD                 "fox_pre_build"

#define TW_USE_COMPRESSION_VAR      "tw_use_compression"
#define TW_FILENAME                 "tw_filename"
#define TW_ZIP_INDEX                "tw_zip_index"
#define TW_ZIP_QUEUE_COUNT       "tw_zip_queue_count"

#define MAX_BACKUP_NAME_LEN 64
#define TW_BACKUP_TEXT              "tw_backup_text"
#define TW_BACKUP_NAME		        "tw_backup_name"
#define TW_BACKUP_SYSTEM_VAR        "tw_backup_system"
#define TW_BACKUP_DATA_VAR          "tw_backup_data"
#define TW_BACKUP_BOOT_VAR          "tw_backup_boot"
#define TW_BACKUP_RECOVERY_VAR      "tw_backup_recovery"
#define TW_BACKUP_CACHE_VAR         "tw_backup_cache"
#define TW_BACKUP_ANDSEC_VAR        "tw_backup_andsec"
#define TW_BACKUP_SDEXT_VAR         "tw_backup_sdext"
#define TW_BACKUP_AVG_IMG_RATE      "tw_backup_avg_img_rate"
#define TW_BACKUP_AVG_FILE_RATE     "tw_backup_avg_file_rate"
#define TW_BACKUP_AVG_FILE_COMP_RATE    "tw_backup_avg_file_comp_rate"
#define TW_BACKUP_SYSTEM_SIZE       "tw_backup_system_size"
#define TW_BACKUP_DATA_SIZE         "tw_backup_data_size"
#define TW_BACKUP_BOOT_SIZE         "tw_backup_boot_size"
#define TW_BACKUP_RECOVERY_SIZE     "tw_backup_recovery_size"
#define TW_BACKUP_CACHE_SIZE        "tw_backup_cache_size"
#define TW_BACKUP_ANDSEC_SIZE       "tw_backup_andsec_size"
#define TW_BACKUP_SDEXT_SIZE        "tw_backup_sdext_size"
#define TW_STORAGE_FREE_SIZE        "tw_storage_free_size"
#define TW_GENERATE_DIGEST_TEXT     "tw_generate_digest_text"

#define TW_RESTORE_TEXT             "tw_restore_text"
#define TW_RESTORE_SYSTEM_VAR       "tw_restore_system"
#define TW_RESTORE_DATA_VAR         "tw_restore_data"
#define TW_RESTORE_BOOT_VAR         "tw_restore_boot"
#define TW_RESTORE_RECOVERY_VAR     "tw_restore_recovery"
#define TW_RESTORE_CACHE_VAR        "tw_restore_cache"
#define TW_RESTORE_ANDSEC_VAR       "tw_restore_andsec"
#define TW_RESTORE_SDEXT_VAR        "tw_restore_sdext"
#define TW_RESTORE_AVG_IMG_RATE     "tw_restore_avg_img_rate"
#define TW_RESTORE_AVG_FILE_RATE    "tw_restore_avg_file_rate"
#define TW_RESTORE_AVG_FILE_COMP_RATE    "tw_restore_avg_file_comp_rate"
#define TW_RESTORE_FILE_DATE        "tw_restore_file_date"
#define TW_VERIFY_DIGEST_TEXT       "tw_verify_digest_text"
#define TW_UPDATE_SYSTEM_DETAILS_TEXT "tw_update_system_details_text"

#define TW_VERSION_VAR              "tw_version"
#define TW_GUI_SORT_ORDER           "tw_gui_sort_order"
#define TW_ZIP_LOCATION_VAR         "tw_zip_location"
#define TW_ZIP_INTERNAL_VAR         "tw_zip_internal"
#define TW_ZIP_EXTERNAL_VAR         "tw_zip_external"
#define TW_DISABLE_FREE_SPACE_VAR   "tw_disable_free_space"
#define TW_FORCE_DIGEST_CHECK_VAR   "tw_force_digest_check"
#define TW_SKIP_DIGEST_CHECK_VAR    "tw_skip_digest_check"
#define TW_SKIP_DIGEST_GENERATE_VAR "tw_skip_digest_generate"
#define TW_SIGNED_ZIP_VERIFY_VAR    "tw_signed_zip_verify"
#define TW_INSTALL_REBOOT_VAR       "tw_install_reboot"
#define TW_TIME_ZONE_VAR            "tw_time_zone"
#define TW_RM_RF_VAR                "tw_rm_rf"

#define TW_BACKUPS_FOLDER_VAR       "tw_backups_folder"

#define TW_SDEXT_SIZE               "tw_sdext_size"
#define TW_SWAP_SIZE                "tw_swap_size"
#define TW_SDPART_FILE_SYSTEM       "tw_sdpart_file_system"
#define TW_TIME_ZONE_GUISEL         "tw_time_zone_guisel"
#define TW_TIME_ZONE_GUIOFFSET      "tw_time_zone_guioffset"
#define TW_TIME_ZONE_GUIDST         "tw_time_zone_guidst"

#define TW_ACTION_BUSY              "tw_busy"

#define TW_ALLOW_PARTITION_SDCARD   "tw_allow_partition_sdcard"

#define TW_SCREEN_OFF               "tw_screen_off"

#define TW_REBOOT_SYSTEM            "tw_reboot_system"
#define TW_REBOOT_RECOVERY          "tw_reboot_recovery"
#define TW_REBOOT_POWEROFF          "tw_reboot_poweroff"
#define TW_REBOOT_BOOTLOADER        "tw_reboot_bootloader"

#define TW_USE_EXTERNAL_STORAGE     "tw_use_external_storage"
#define TW_HAS_INTERNAL             "tw_has_internal"
#define TW_INTERNAL_PATH            "tw_internal_path"         // /data/media or /internal
#define TW_INTERNAL_MOUNT           "tw_internal_mount"        // /data or /internal
#define TW_INTERNAL_LABEL           "tw_internal_label"        // data or internal
#define TW_HAS_EXTERNAL             "tw_has_external"
#define TW_EXTERNAL_PATH            "tw_external_path"         // /sdcard or /external/sdcard2
#define TW_EXTERNAL_MOUNT           "tw_external_mount"        // /sdcard or /external
#define TW_EXTERNAL_LABEL           "tw_external_label"        // sdcard or external

#define TW_HAS_DATA_MEDIA           "tw_has_data_media"

#define TW_HAS_BOOT_PARTITION       "tw_has_boot_partition"
#define TW_HAS_RECOVERY_PARTITION   "tw_has_recovery_partition"
#define TW_HAS_ANDROID_SECURE       "tw_has_android_secure"
#define TW_HAS_SDEXT_PARTITION      "tw_has_sdext_partition"
#define TW_HAS_USB_STORAGE          "tw_has_usb_storage"
#define TW_NO_BATTERY_PERCENT       "tw_no_battery_percent"
#define TW_POWER_BUTTON             "tw_power_button"
#define TW_SIMULATE_ACTIONS         "tw_simulate_actions"
#define TW_SIMULATE_FAIL            "tw_simulate_fail"
#define TW_DONT_UNMOUNT_SYSTEM      "tw_dont_unmount_system"
// #define TW_ALWAYS_RMRF              "tw_always_rmrf"

#define TW_SHOW_DUMLOCK             "tw_show_dumlock"
#define TW_HAS_INJECTTWRP           "tw_has_injecttwrp"
#define TW_INJECT_AFTER_ZIP         "tw_inject_after_zip"
#define TW_HAS_DATADATA             "tw_has_datadata"
#define TW_FLASH_ZIP_IN_PLACE       "tw_flash_zip_in_place"
#define TW_MIN_SYSTEM_SIZE          "50" // minimum system size to allow a reboot
#define TW_MIN_SYSTEM_VAR           "tw_min_system"
#define TW_DOWNLOAD_MODE            "tw_download_mode"
#define TW_IS_ENCRYPTED             "tw_is_encrypted"
#define TW_IS_DECRYPTED             "tw_is_decrypted"
#define TW_CRYPTO_PWTYPE            "tw_crypto_pwtype"
#define TW_HAS_CRYPTO               "tw_has_crypto"
#define TW_IS_FBE                   "tw_is_fbe"
#define TW_CRYPTO_PASSWORD          "tw_crypto_password"
#define TW_SDEXT_DISABLE_EXT4       "tw_sdext_disable_ext4"
#define TW_MILITARY_TIME            "tw_military_time"
#define TW_USE_SHA2                 "tw_use_sha2"
#define TW_NO_SHA2                  "tw_no_sha2"

// Also used:
//   tw_boot_is_mountable
//   tw_system_is_mountable
//   tw_data_is_mountable
//   tw_cache_is_mountable
//   tw_sdcext_is_mountable
//   tw_sdcint_is_mountable
//   tw_sd-ext_is_mountable
//   tw_sp1_is_mountable
//   tw_sp2_is_mountable
//   tw_sp3_is_mountable

// Max archive size for tar backups before we split (1.5GB)
#define MAX_ARCHIVE_SIZE 1610612736LLU
//#define MAX_ARCHIVE_SIZE 52428800LLU // 50MB split for testing

#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/class/android_usb/android0/f_mass_storage/lun%d/file"
#endif

// For OpenRecoveryScript
#define SCRIPT_FILE_CACHE "/cache/recovery/openrecoveryscript"
#define SCRIPT_FILE_TMP "/tmp/openrecoveryscript"
#define TMP_LOG_FILE "/tmp/recovery.log"

#endif  // _VARIABLES_HEADER_
