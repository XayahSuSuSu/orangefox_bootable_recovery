/*
	Copyright 2012 to 2017 bigbiff/Dees_Troy TeamWin
	
	Copyright (C) 2018-2020 OrangeFox Recovery Project
	This file is part of the OrangeFox Recovery Project.
	
	This file is part of TWRP/TeamWin Recovery Project.
	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <iostream>
#include <fstream>

#include <string.h>
#include <stdio.h>
#include <cutils/properties.h>

#include <android-base/unique_fd.h>

#include "twcommon.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"

#include "otautil/sysutil.h"
#include <ziparchive/zip_archive.h>
#include "twinstall/install.h"
#include "twinstall/verifier.h"
#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrpDigestDriver.hpp"
#include "twrpDigest/twrpDigest.hpp"
#include "twrpDigest/twrpMD5.hpp"
#include "twrp-functions.hpp"
#include "orangefox.hpp"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/blanktimer.hpp"
#include "legacy_property_service.h"
#include "twinstall.h"
#include "installcommand.h"
extern "C" {
	#include "gui/gui.h"
}

#define AB_OTA "payload_properties.txt"

#ifndef TW_NO_LEGACY_PROPS
static const char *properties_path = "/dev/__properties__";
static const char *properties_path_renamed = "/dev/__properties_kk__";
static bool legacy_props_env_initd = false;
static bool legacy_props_path_modified = false;
#endif

enum zip_type
{
  UNKNOWN_ZIP_TYPE = 0,
  UPDATE_BINARY_ZIP_TYPE,
  AB_OTA_ZIP_TYPE,
  TWRP_THEME_ZIP_TYPE
};

#ifndef TW_NO_LEGACY_PROPS
// to support pre-KitKat update-binaries that expect properties in the legacy format
static int switch_to_legacy_properties()
{
	if (!legacy_props_env_initd) {
		if (legacy_properties_init() != 0)
			return -1;

		char tmp[32];
		int propfd, propsz;
		legacy_get_property_workspace(&propfd, &propsz);
		sprintf(tmp, "%d,%d", dup(propfd), propsz);
		setenv("ANDROID_PROPERTY_WORKSPACE", tmp, 1);
		legacy_props_env_initd = true;
	}

	if (TWFunc::Path_Exists(properties_path)) {
		// hide real properties so that the updater uses the envvar to find the legacy format properties
		if (rename(properties_path, properties_path_renamed) != 0) {
			LOGERR("Renaming %s failed: %s\n", properties_path, strerror(errno));
			return -1;
		} else {
			legacy_props_path_modified = true;
		}
	}

	return 0;
}

static int switch_to_new_properties()
{
	if (TWFunc::Path_Exists(properties_path_renamed)) {
		if (rename(properties_path_renamed, properties_path) != 0) {
			LOGERR("Renaming %s failed: %s\n", properties_path_renamed, strerror(errno));
			return -1;
		} else {
			legacy_props_path_modified = false;
		}
	}

	return 0;
}
#endif

static int Install_Theme(const char* path, ZipArchiveHandle Zip) {
#ifdef TW_OEM_BUILD // We don't do custom themes in OEM builds
	CloseArchive(Zip);
	return INSTALL_CORRUPT;
#else
	ZipString binary_name("ui.xml");
	ZipEntry binary_entry;
	if (FindEntry(Zip, binary_name, &binary_entry) != 0) {
		CloseArchive(Zip);
		return INSTALL_CORRUPT;
	}
	if (!PartitionManager.Mount_Settings_Storage(true))
		return INSTALL_ERROR;
	string theme_path = DataManager::GetSettingsStoragePath();
	theme_path += Fox_Themes_Dir;
	if (!TWFunc::Path_Exists(theme_path)) {
		if (!TWFunc::Recursive_Mkdir(theme_path)) {
			return INSTALL_ERROR;
		}
	}
	theme_path += "/ui.zip";
	if (TWFunc::copy_file(path, theme_path, 0644) != 0) {
		return INSTALL_ERROR;
	}
	LOGINFO("Installing custom theme '%s' to '%s'\n", path, theme_path.c_str());
	PageManager::RequestReload();
	return INSTALL_SUCCESS;
#endif
}

static int Prepare_Update_Binary(const char *path, ZipArchiveHandle Zip) {
  string bootloader = "firmware-update/emmc_appsboot.mbn";
  string metadata_sg_path = "META-INF/com/android/metadata";
  string fingerprint_property = "ro.build.fingerprint";
  string pre_device = "pre-device";
  string pre_build = "pre-build";
  string mCheck = "";
  string miui_check1 = "ro.miui.ui.version";
  string check_command = "grep " + miui_check1 + " " + TMP_UPDATER_BINARY_PATH;
  string zip_name = path;
  int is_new_miui_update_binary = 0;
  string assert_device = "";
  zip_is_rom_package = false; 		// assume we are not installing a ROM
  zip_is_survival_trigger = false; 	// assume non-miui
  support_all_block_ota = false; 	// non-MIUI block-based OTA updates
  zip_is_for_specific_build = false;
  DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 0); // assume standard zip installer
  DataManager::SetValue(FOX_ZIP_INSTALLER_TREBLE, "0");
  DataManager::SetValue("found_fox_overwriting_rom", "0");

	char arches[PATH_MAX];
	property_get("ro.product.cpu.abilist", arches, "error");
	if (strcmp(arches, "error") == 0)
		property_get("ro.product.cpu.abi", arches, "error");
	vector<string> split = TWFunc::split_string(arches, ',', true);
	std::vector<string>::iterator arch;
	std::string base_name = UPDATE_BINARY_NAME;
	base_name += "-";
	ZipEntry binary_entry;
	ZipString update_binary_string(UPDATE_BINARY_NAME);
	if (FindEntry(Zip, update_binary_string, &binary_entry) != 0) {
		for (arch = split.begin(); arch != split.end(); arch++) {
			std::string temp = base_name + *arch;
			ZipString binary_name(temp.c_str());
			if (FindEntry(Zip, binary_name, &binary_entry) != 0) {
				ZipString binary_name(temp.c_str());
				break;
			}
		}
	}
	LOGINFO("Extracting updater binary '%s'\n", UPDATE_BINARY_NAME);
	unlink(TMP_UPDATER_BINARY_PATH);
	android::base::unique_fd fd(
		open(TMP_UPDATER_BINARY_PATH, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0755));
	if (fd == -1) {
		return INSTALL_ERROR;
	}
	int32_t err = ExtractEntryToFile(Zip, &binary_entry, fd);
	if (err != 0) {
		CloseArchive(Zip);
		LOGERR("Could not extract '%s'\n", UPDATE_BINARY_NAME);
		return INSTALL_ERROR;
	}

// -------------- OrangeFox: start ---------------- //
  if (DataManager::GetIntValue(FOX_INSTALL_PREBUILT_ZIP) != 1)
    {
      DataManager::SetValue(FOX_METADATA_PRE_BUILD, 0);
      DataManager::SetValue(FOX_MIUI_ZIP_TMP, 0);
      DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 0);
      DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 0);
      DataManager::SetValue(FOX_LOADED_FINGERPRINT, 0);

      gui_msg("fox_install_detecting=Detecting Current Package");
      
      if (zip_EntryExists(Zip, UPDATER_SCRIPT))
	{
	  if (zip_ExtractEntry(Zip, UPDATER_SCRIPT, FOX_TMP_PATH, 0644))
	    {
	      if (Installing_ROM_Query(FOX_TMP_PATH, Zip))
	        {
		  zip_is_rom_package = true;
		  DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 1); // standard ROM
		  
	          // check for embedded recovery installs
	          if  (
	                 (TWFunc::CheckWord(FOX_TMP_PATH, "/dev/block/bootdevice/by-name/recovery")
	              && (TWFunc::CheckWord(FOX_TMP_PATH, "recovery.img") || TWFunc::CheckWord(FOX_TMP_PATH, "twrp.img"))
	              && (zip_EntryExists(Zip, "recovery.img") || zip_EntryExists(Zip, "twrp.img") || zip_EntryExists(Zip, "recovery/twrp.img") || zip_EntryExists(Zip, "recovery/recovery.img"))
	                 )) {
	                  DataManager::SetValue("found_fox_overwriting_rom", "1");
	                  usleep(32);
	                  TWFunc::Check_OrangeFox_Overwrite_FromROM(true, path);
	             }
		} 
	      assert_device = Fox_CheckForAsserts();
	      unlink(FOX_TMP_PATH);	      
	    } 
	} 

   // try to identify MIUI ROM installer
      if (zip_is_rom_package == true) 
         {
            if (zip_EntryExists(Zip, FOX_MIUI_UPDATE_PATH)) // META-INF/com/miui/miui_update - if found, then this is a miui zip installer
              {
                zip_is_survival_trigger = true;
                support_all_block_ota = true;
                LOGINFO("OrangeFox: Detected miui_update file [%s]\n", FOX_MIUI_UPDATE_PATH);
              }
            else // do another check for miui
             {
              mCheck = TWFunc::Exec_With_Output(check_command);  // check for miui in update-binary             
              if (mCheck.size() > 0)
                { 
                  LOGINFO("OrangeFox: the answer that I received is: [%s]\n",mCheck.c_str());
                  int not_found = mCheck.find("not found");
                  if (not_found == -1) // then we are miui
                    {    
                       LOGINFO("OrangeFox: Detected new Xiaomi update-binary [Message=%s]\n", mCheck.c_str());
                       is_new_miui_update_binary = 1;
                       zip_is_survival_trigger = true;
                       support_all_block_ota = true;
                    }
                   else 
                     {
                        LOGINFO("OrangeFox: Received a response from [%s], but did not detect a new Xiaomi update-binary -Message=[%s] and code=[%i]\n", 
                    	    check_command.c_str(), mCheck.c_str(), not_found);
                     }
                } // mCheck.size > 0
              else
                {
                   LOGINFO("OrangeFox: The output of [%s] came out empty\n", check_command.c_str());
                }  
           }
       }
   // 

   if (zip_is_rom_package == true) 
   {
      if (zip_is_survival_trigger == true) // MIUI installer
         {
	  	gui_msg ("fox_install_miui_detected=- Detected MIUI Update Package");
	      	DataManager::SetValue(FOX_MIUI_ZIP_TMP, 1);
	      	DataManager::SetValue(FOX_CALL_DEACTIVATION, 1);
	      	DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 2); // MIUI ROM
	  	support_all_block_ota = true;
         }
      else
         {
	       DataManager::SetValue(FOX_CALL_DEACTIVATION, 1);
	       DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 1); // standard ROM
	       gui_msg ("fox_install_standard_detected=- Detected standard ROM zip installer");
	       support_all_block_ota = Fox_Support_All_OTA();
         }   
   }

   // treble ROM ?
   if (zip_is_rom_package == true) 
    {
      if ((zip_EntryExists(Zip, "vendor.new.dat")) || (zip_EntryExists(Zip, "vendor.new.dat.br"))) // we are installing a Treble ROM
         {
           DataManager::SetValue(FOX_ZIP_INSTALLER_TREBLE, "1");
           Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
           usleep (32);
           
           if (Fox_Zip_Installer_Code == 1) // custom
                 DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 11); // custom Treble ROM
           
           if (Fox_Zip_Installer_Code == 2) // miui 
                 DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 22); // miui Treble ROM
           
           Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
           LOGINFO("OrangeFox: detected Treble ROM installer. [code=%i] \n", Fox_Zip_Installer_Code);
        }
        else 
        if (TWFunc::Has_Vendor_Partition())
         {
           DataManager::SetValue(FOX_ZIP_INSTALLER_TREBLE, "1");
           Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
           usleep (32);
           
           if (Fox_Zip_Installer_Code == 1) // custom
                 DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 11);
           
           if (Fox_Zip_Installer_Code == 2) // miui 
                 DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 22);
           
           Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
           LOGINFO("OrangeFox: detected standard ROM installer, on a real Treble device!\n");       
         }
    
    	if (Fox_OTA_Backup_Stock_Boot_Image())
    	  {
    	    if (support_all_block_ota && zip_EntryExists(Zip, "boot.img"))
      	     {
      		if (zip_ExtractEntry(Zip, "boot.img", boot_bak_img, 0644))
      		 {
      	    	   LOGINFO("Making a temporary copy of the stock boot image.\n");
      	   	}
      	     }
      	 }
    }
   // treble

   // do we need to do an OTA_RES first?
   if (Fox_Skip_OTA()) // no
   {
     // LOGINFO("OrangeFox: not executing incremental OTA restore (OTA_RES)\n");   
   } 
   else
   {
     if (zip_is_survival_trigger || support_all_block_ota)
	{
	  if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) != 0)
	    gui_msg
	       ("fox_incremental_ota_status_enabled=Support Incremental package status: Enabled");
	  
	  if (zip_EntryExists(Zip, metadata_sg_path)) // META-INF/com/android/metadata is in zip
	    {
	      const string take_out_metadata = "/tmp/build.prop";
	      if (zip_ExtractEntry(Zip, metadata_sg_path, take_out_metadata, 0644))
		{
		  string metadata_fingerprint = TWFunc::File_Property_Get(take_out_metadata, pre_build); // look for "pre-build"
		  string metadata_device = TWFunc::File_Property_Get(take_out_metadata, pre_device);  // look for "pre-device"

		  string fingerprint = TWFunc::System_Property_Get(fingerprint_property); // try to get system fingerprint - ro.build.fingerprint
		  if (fingerprint.empty()) {
   			fingerprint = TWFunc::Fox_Property_Get("orangefox.system.fingerprint");
   			if (fingerprint.empty()) {
      			    fingerprint = TWFunc::File_Property_Get(orangefox_cfg, "ROM_FINGERPRINT");
   			}
		  }

		  // appropriate "pre-build" entry in META-INF/com/android/metadata ? == incremental block-based OTA zip installer
		  if (metadata_fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE) 
		    {
		      gui_msg(Msg
			      ("fox_incremental_package_detected=Detected Incremental package '{1}'")
			      (path));
			      
		      if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) == 0)
		      {
		  	CloseArchive(Zip);
		  	LOGERR("Incremental OTA is not enabled. Quitting the incremental OTA update.\n");
		  	set_miui_install_status(OTA_ERROR, false);
		  	return INSTALL_ERROR; 
		      }

		      // --- check for mismatching fingerprints when they should match
		      string metadata_prebuild_incremental = TWFunc::File_Property_Get(take_out_metadata, "pre-build-incremental");
		      string metadata_ota_type = TWFunc::File_Property_Get(take_out_metadata, "ota-type");
		      string orangefox_incremental = TWFunc::File_Property_Get(orangefox_cfg, "INCREMENTAL_VERSION");

		      if (metadata_fingerprint != fingerprint)
		      {
    			    DataManager::GetValue(FOX_COMPATIBILITY_DEVICE, Fox_Current_Device);
   			    if (metadata_device == Fox_Current_Device && metadata_prebuild_incremental == orangefox_incremental) {
       				LOGINFO("- DEBUG: OrangeFox: metadata_fingerprint != system_fingerprint. Trying to fix it.\n- Changing [%s] to [%s]\n",
           				fingerprint.c_str(), metadata_fingerprint.c_str());
       				string atmp = "\"";
       				usleep(4096);
       				TWFunc::Exec_Cmd("/sbin/resetprop ro.build.fingerprint " + atmp + metadata_fingerprint + atmp);
       				usleep(250000);
       				TWFunc::Exec_Cmd("/sbin/resetprop orangefox.system.fingerprint " + atmp + metadata_fingerprint + atmp);
       				usleep(100000);
   			     }
		        }
			// ---

		      zip_is_for_specific_build = true;
		      
		      DataManager::SetValue(FOX_METADATA_PRE_BUILD, 1);
		      
		      if ((fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE) 
		      && (DataManager::GetIntValue("fox_verify_incremental_ota_signature") != 0))
			{
			  gui_msg
			    ("fox_incremental_ota_compatibility_chk=Verifying Incremental Package Signature...");

			    if (verify_incremental_package 
			         (fingerprint, 
			          metadata_fingerprint,
				  metadata_device))
			    {
			      gui_msg("fox_incremental_ota_compatibility_true=Incremental package is compatible.");
			      property_set(fingerprint_property.c_str(), metadata_fingerprint.c_str());
			      DataManager::SetValue(FOX_LOADED_FINGERPRINT, metadata_fingerprint);
			    }
			  else
			    {
			      set_miui_install_status(OTA_VERIFY_FAIL, false);
			      gui_err("fox_incremental_ota_compatibility_false=Incremental package isn't compatible with this ROM!");
			      return INSTALL_ERROR;
			    }
			}
		      else
			{
			  property_set(fingerprint_property.c_str(), metadata_fingerprint.c_str());
			}
		    
		      if (zip_is_for_specific_build)
			 {
			   if (Fox_Fix_OTA_Update_Manual_Flash_Error())
			   {
			   	if ((!ors_is_active()) && (zip_is_rom_package))
			       	   LOGERR("You must flash incremental OTA updates from the ROM's updater, because only the ROM can decrypt the zips.\n");
			   }
			 }			
		      unlink(take_out_metadata.c_str());		      
		    }
		}
	      else
		{
		  CloseArchive(Zip);
		  LOGERR("Could not extract '%s'\n", take_out_metadata.c_str());
		  set_miui_install_status(OTA_ERROR, false);
		  return INSTALL_ERROR;
		}
	    }
	}
      else
	{
            if (zip_is_rom_package == true) 	       
              gui_msg ("fox_incremental_ota_status_disabled=Support Incremental package status: Disabled");
	}

      string ota_location_folder, ota_location_backup, loadedfp;
      DataManager::GetValue(FOX_SURVIVAL_FOLDER_VAR, ota_location_folder);
      DataManager::GetValue(FOX_SURVIVAL_BACKUP_NAME, ota_location_backup);
      ota_location_folder += "/" + ota_location_backup;
      DataManager::GetValue(FOX_LOADED_FINGERPRINT, loadedfp);

      if (DataManager::GetIntValue(FOX_METADATA_PRE_BUILD) != 0
	  && !TWFunc::Verify_Loaded_OTA_Signature(loadedfp, ota_location_folder))
	{
	  TWPartition *survival_boot =
	    PartitionManager.Find_Partition_By_Path("/boot");

	  if (!survival_boot)
	    {
	      set_miui_install_status(OTA_ERROR, false);
	      LOGERR("OTA_Survival: Unable to find boot partition\n");
	      return INSTALL_ERROR;
	    }
	    
	  TWPartition *survival_sys =
	    PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
	    
	  if (!survival_sys)
	    {
	      set_miui_install_status(OTA_ERROR, false);
	      LOGERR("OTA_Survival: Unable to find system partition\n");
	      return INSTALL_ERROR;
	    }

	  std::string action;
	  DataManager::GetValue("tw_action", action);
	  if (action != "openrecoveryscript"
	      && DataManager::GetIntValue(FOX_MIUI_ZIP_TMP) != 0)
	    {
	      if (Fox_Fix_OTA_Update_Manual_Flash_Error())
	      {
	    	std::string cachefile = TWFunc::get_log_dir(); //"/cache/recovery/openrecoveryscript";
	    	if (cachefile == DATA_LOGS_DIR)
		   cachefile = "/data/cache";
		cachefile = cachefile + "/recovery/openrecoveryscript";

	    	gui_print_color("warning", "\n\n- You tried to flash OTA zip (%s) manually. Attempting to recover the situation...\n\n", path);
	    	TWFunc::CreateNewFile(cachefile);
	    	usleep(256);
	    	if (TWFunc::Path_Exists(cachefile))
	       	   {
	    	   	TWFunc::AppendLineToFile(cachefile, "install " + zip_name);
	    	   	usleep(256);
	    	   	CloseArchive(Zip);
	    	   	usleep(256);
	    	   	gui_print_color("warning", "\n- Rebooting into OTA install mode in 10 seconds. Please wait ...\n\n");
	    	   	sleep(10);
	    	   	TWFunc::tw_reboot(rb_recovery);
	    	   	return INSTALL_ERROR;
	       	   }
	      }
	      LOGERR("Please flash this package using the ROM's updater app!\n");
	      return INSTALL_ERROR;
	    }

	  string Boot_File = ota_location_folder + "/boot.emmc.win";
	  if (
	     (!storage_is_encrypted()) || (TWFunc::Path_Exists(Boot_File)) || (DataManager::GetIntValue("OTA_decrypted") == 1)
	     )
	    {
	      if (Fox_OTA_RES_Check_MicroSD())
	      {
	      	 if (!TWFunc::Path_Exists(Boot_File)) 
	      	  {
		    string atmp_ota = "/sdcard1/Fox/OTA/boot.emmc.win";
		    if (TWFunc::Path_Exists(atmp_ota)) {
		    	Boot_File = atmp_ota;
		    	ota_location_folder = "/sdcard1/Fox/OTA";
		    	gui_print("- OTA backup found in /sdcard1/Fox/OTA/ - trying that instead.\n");
		    }
	      	}
	      }
	      
	      if (TWFunc::Path_Exists(Boot_File))
		{
		  gui_msg("fox_incremental_ota_res_run=Running restore process of the current OTA file");
		  DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 1);
		  PartitionManager.Set_Restore_Files(ota_location_folder);
		  if (PartitionManager.Run_OTA_Survival_Restore(ota_location_folder))
		    {
		      gui_msg("fox_incremental_ota_res=Process OTA_RES -- done!!");
		    }
		  else
		    {
		      set_miui_install_status(OTA_ERROR, false);
		      LOGERR("OTA_Survival: Unable to finish OTA_RES!\n");
		      return INSTALL_ERROR;
		    }
		}
	      else
		{
		  set_miui_install_status(OTA_CORRUPT, false);
		  gui_err("fox_survival_does_not_exist=OTA Survival does not exist! Please flash a full ROM first!");
		  return INSTALL_ERROR;
		}
	    }
	  else
	    {
	      set_miui_install_status(OTA_CORRUPT, false);
	      gui_print ("Internal storage is encrypted! Please do decrypt first!\n");
	      return INSTALL_ERROR;
	    }
	}
    } // Fox_Skip_OTA()
    
    if (zip_EntryExists(Zip, bootloader))
	gui_msg(Msg
		(msg::kWarning,
		 "fox_zip_have_bootloader=Warning: OrangeFox detected bootloader inside of the {1}")
		(path));
    }

  if (blankTimer.isScreenOff())
    {
      if (zip_EntryExists(Zip, AROMA_CONFIG))
	{
	  blankTimer.toggleBlank();
	  gui_changeOverlay("");
	}
    }
// -------------- OrangeFox: end ---------------- //

	// If exists, extract file_contexts from the zip file
	ZipString file_contexts("file_contexts");
	ZipEntry file_contexts_entry;
	if (FindEntry(Zip, file_contexts, &file_contexts_entry) != 0) {
		LOGINFO("Zip does not contain SELinux file_contexts file in its root.\n");
	} else {
		const string output_filename = "/file_contexts";
		LOGINFO("Zip contains SELinux file_contexts file in its root. Extracting to %s\n", output_filename.c_str());
		android::base::unique_fd fd(
			open(output_filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644));
		if (fd == -1) {
			return INSTALL_ERROR;
		}
		if (ExtractEntryToFile(Zip, &file_contexts_entry, fd)) {
			CloseArchive(Zip);
			LOGERR("Could not extract '%s'\n", output_filename.c_str());
			return INSTALL_ERROR;
		}
	}

  	Fox_ProcessAsserts(assert_device);
  	
	return INSTALL_SUCCESS;
}

#ifndef TW_NO_LEGACY_PROPS
static bool update_binary_has_legacy_properties(const char *binary) {
	const char str_to_match[] = "ANDROID_PROPERTY_WORKSPACE";
	int len_to_match = sizeof(str_to_match) - 1;
	bool found = false;

	int fd = open(binary, O_RDONLY);
	if (fd < 0) {
		LOGINFO("has_legacy_properties: Could not open %s: %s!\n", binary, strerror(errno));
		return false;
	}

	struct stat finfo;
	if (fstat(fd, &finfo) < 0) {
		LOGINFO("has_legacy_properties: Could not fstat %d: %s!\n", fd, strerror(errno));
		close(fd);
		return false;
	}

	void *data = mmap(NULL, finfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED) {
		LOGINFO("has_legacy_properties: mmap (size=%zu) failed: %s!\n", (size_t)finfo.st_size, strerror(errno));
	} else {
		if (memmem(data, finfo.st_size, str_to_match, len_to_match)) {
			LOGINFO("has_legacy_properties: Found legacy property match!\n");
			found = true;
		}
		munmap(data, finfo.st_size);
	}
	close(fd);

	return found;
}
#endif

static int Run_Update_Binary(const char *path, int* wipe_cache, zip_type ztype) {
	int ret_val, pipe_fd[2], status, zip_verify, aroma_running;;
	char buffer[1024];
	FILE* child_data;

#ifndef TW_NO_LEGACY_PROPS
	if (!update_binary_has_legacy_properties(TMP_UPDATER_BINARY_PATH)) {
		LOGINFO("Legacy property environment not used in updater.\n");
	} else if (switch_to_legacy_properties() != 0) { /* Set legacy properties */
		LOGERR("Legacy property environment did not initialize successfully. Properties may not be detected.\n");
	} else {
		LOGINFO("Legacy property environment initialized.\n");
	}
#endif

	pipe(pipe_fd);

	std::vector<std::string> args;
    if (ztype == UPDATE_BINARY_ZIP_TYPE) {
		ret_val = update_binary_command(path, 0, pipe_fd[1], &args);
    } else if (ztype == AB_OTA_ZIP_TYPE) {
		ret_val = abupdate_binary_command(path, 0, pipe_fd[1], &args);
	} else {
		LOGERR("Unknown zip type %i\n", ztype);
		ret_val = INSTALL_CORRUPT;
	}
    if (ret_val) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return ret_val;
    }

	// Convert the vector to a NULL-terminated char* array suitable for execv.
	const char* chr_args[args.size() + 1];
	chr_args[args.size()] = NULL;
	for (size_t i = 0; i < args.size(); i++)
		chr_args[i] = args[i].c_str();

	pid_t pid = fork();
	if (pid == 0) {
		close(pipe_fd[0]);
		execve(chr_args[0], const_cast<char**>(chr_args), environ);
		printf("E:Can't execute '%s': %s\n", chr_args[0], strerror(errno));
		_exit(-1);
	}
	close(pipe_fd[1]);

  	aroma_running = 0;
	*wipe_cache = 0;

	DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
	child_data = fdopen(pipe_fd[0], "r");
	while (fgets(buffer, sizeof(buffer), child_data) != NULL) {
		char* command = strtok(buffer, " \n");
		if (command == NULL) {
			continue;
		} else if (strcmp(command, "progress") == 0) {
			char* fraction_char = strtok(NULL, " \n");
			char* seconds_char = strtok(NULL, " \n");

			float fraction_float = strtof(fraction_char, NULL);
			int seconds_float = strtol(seconds_char, NULL, 10);

			if (zip_verify)
				DataManager::ShowProgress(fraction_float * (1 - VERIFICATION_PROGRESS_FRACTION), seconds_float);
			else
				DataManager::ShowProgress(fraction_float, seconds_float);
		} else if (strcmp(command, "set_progress") == 0) {
			char* fraction_char = strtok(NULL, " \n");
			float fraction_float = strtof(fraction_char, NULL);
			DataManager::_SetProgress(fraction_float);
		} else if (strcmp(command, "ui_print") == 0) {
			char* display_value = strtok(NULL, "\n");
	  		if (display_value) {
	      		     if (strcmp(display_value, "AROMA Filemanager Finished...") == 0 && (aroma_running == 1)) {
		  		aroma_running = 0;
		  		gui_changeOverlay("");
		  		TWFunc::copy_file(Fox_aroma_cfg, Fox_sdcard_aroma_cfg, 0644);
			     }
	      		    gui_print("%s", display_value);
	      		    if (strcmp(display_value, "(c) 2013-2015 by amarullz.com") == 0 && (aroma_running == 0)) {
		  		aroma_running = 1;
		  		gui_changeOverlay("black_out");
		  		TWFunc::copy_file(Fox_aroma_cfg, Fox_sdcard_aroma_cfg, 0644);
			     }
	    		}
	  		else {
	      			gui_print("\n");
	    		}
		} else if (strcmp(command, "wipe_cache") == 0) {
			*wipe_cache = 1;
		} else if (strcmp(command, "clear_display") == 0) {
			// Do nothing, not supported by TWRP
		} else if (strcmp(command, "log") == 0) {
			printf("%s\n", strtok(NULL, "\n"));
		} else {
			LOGERR("unknown command [%s]\n", command);
		}
	}
	fclose(child_data);

	int waitrc = TWFunc::Wait_For_Child(pid, &status, "Updater");

  	// Should never happen, but in case of crash or other unexpected condition
  	if (aroma_running == 1) {
      		gui_changeOverlay("");
    	}

  	// if updater-script doesn't find the correct device
  	if (WEXITSTATUS (status) == TW_ERROR_WRONG_DEVICE) {
       		gui_print_color("error", "\nPossible causes of this error:\n  1. Wrong device\n  2. Wrong firmware\n  3. Corrupt zip\n  4. System not mounted\n  5. Bugged updater-script.\n\nSearch online for \"error %i\". ",
       			TW_ERROR_WRONG_DEVICE);
       		gui_print_color("error", "Check \"/tmp/recovery.log\", and look above, for the specific cause of this error.\n\n");
     	}

#ifndef TW_NO_LEGACY_PROPS
	/* Unset legacy properties */
	if (legacy_props_path_modified) {
		if (switch_to_new_properties() != 0) {
			LOGERR("Legacy property environment did not disable successfully. Legacy properties may still be in use.\n");
		} else {
			LOGINFO("Legacy property environment disabled.\n");
		}
	}
#endif

  	if (waitrc != 0) {
      		set_miui_install_status(OTA_CORRUPT, false);
      		return INSTALL_ERROR;
    	}

	return INSTALL_SUCCESS;
}

int TWinstall_zip(const char *path, int *wipe_cache)
{
  int ret_val, zip_verify = 1, unmount_system = 1, unmount_vendor = 1;

  if (strcmp(path, "error") == 0)
    {
      LOGERR("Failed to get adb sideload file: '%s'\n", path);
      return INSTALL_CORRUPT;
    }

  if (DataManager::GetIntValue(FOX_INSTALL_PREBUILT_ZIP) == 1)
     {
         DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 0); // internal zip = standard zip installer
         DataManager::SetValue(FOX_ZIP_INSTALLER_TREBLE, 0);
     }    
  else   
    {
	gui_msg(Msg("installing_zip=Installing zip file '{1}'")(path));
	if (strlen(path) < 9 || strncmp(path, "/sideload", 9) != 0) {
		string digest_str;
		string Full_Filename = path;

		gui_msg("check_for_digest=Checking for Digest file...");

		if (*path != '@' && !twrpDigestDriver::Check_File_Digest(Full_Filename)) {
			LOGERR("Aborting zip install: Digest verification failed\n");
		      	set_miui_install_status(OTA_CORRUPT, true);
			return INSTALL_CORRUPT;
		}
	}
    }

  DataManager::GetValue(TW_UNMOUNT_SYSTEM, unmount_system);
  DataManager::GetValue(TW_UNMOUNT_VENDOR, unmount_vendor);

#ifndef TW_OEM_BUILD
  DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
#endif

  DataManager::SetProgress(0);

	auto package = Package::CreateMemoryPackage(path);
	if (!package) {
		return INSTALL_CORRUPT;
	}

	if (zip_verify) {
		gui_msg("verify_zip_sig=Verifying zip signature...");
		static constexpr const char* CERTIFICATE_ZIP_FILE = "/system/etc/security/otacerts.zip";
		std::vector<Certificate> loaded_keys = LoadKeysFromZipfile(CERTIFICATE_ZIP_FILE);
		if (loaded_keys.empty()) {
			LOGERR("Failed to load keys\n");
			return -1;
		}
		LOGINFO("%zu key(s) loaded from %s\n", loaded_keys.size(), CERTIFICATE_ZIP_FILE);

		ret_val = verify_file(package.get(), loaded_keys, std::bind(&DataManager::SetProgress, std::placeholders::_1));
		if (ret_val != VERIFY_SUCCESS) {
			LOGINFO("Zip signature verification failed: %i\n", ret_val);
			gui_err("verify_zip_fail=Zip signature verification failed!");
			return -1;
		} else {
			gui_msg("verify_zip_done=Zip signature verified successfully.");
		}
    }
    
    ZipArchiveHandle Zip = package->GetZipArchiveHandle();
    if (!Zip) {
      set_miui_install_status(OTA_CORRUPT, true);
      gui_err("zip_corrupt=Zip file is corrupt!");
      return INSTALL_CORRUPT;
    }

    if (unmount_system) {
	if (PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path())) {
		gui_msg("unmount_system=Unmounting System...");
		if (PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false)) {
			//unlink(PartitionManager.Get_Android_Root_Path().c_str());
			//mkdir(PartitionManager.Get_Android_Root_Path().c_str(), 0755);
		}
		else {
			gui_msg("unmount_system_err=Failed to unmount System");
		        return -1;
		}
	}
   }

   if (unmount_vendor) {
	if (PartitionManager.Is_Mounted_By_Path("/vendor")) {
		gui_msg("unmount_vendor=Unmounting Vendor...");
		if (PartitionManager.UnMount_By_Path("/vendor", false)) {
		   	//unlink("/vendor");
		   	//mkdir("/vendor", 0755);
		} else {
			gui_msg("unmount_vendor_err=Failed to unmount Vendor");
			return -1;
		}
	}
   }

   // DJ9, 20200622: try to avoid a situation where blockimg will bomb out when trying to create a stash
   if ((TWFunc::Path_Exists("/cache/.")) && (!TWFunc::Path_Exists("/cache/recovery/."))) {
	LOGINFO("Recreating the /cache/recovery/ folder ...\n");
	if (!TWFunc::Recursive_Mkdir("/cache/recovery", false))
	   LOGERR("Could not create /cache/recovery - blockimg may have problems with creating stashes\n");
   }
   // DJ9

  time_t start, stop;
  time(&start);
 
  ZipString update_binary_name(UPDATE_BINARY_NAME);
  ZipEntry update_binary_entry;
  if (FindEntry(Zip, update_binary_name, &update_binary_entry) == 0) {
		LOGINFO("Update binary zip\n");
		// Additionally verify the compatibility of the package.
		if (!verify_package_compatibility(Zip)) {
			gui_err("zip_compatible_err=Zip Treble compatibility error!");
			CloseArchive(Zip);
			ret_val = INSTALL_CORRUPT;
		} else {
			ret_val = Prepare_Update_Binary(path, Zip);
			if (ret_val == INSTALL_SUCCESS)
				ret_val = Run_Update_Binary(path, wipe_cache, UPDATE_BINARY_ZIP_TYPE);
		}
	} else {
		ZipString ab_binary_name(AB_OTA);
		ZipEntry ab_binary_entry;
		if (FindEntry(Zip, ab_binary_name, &ab_binary_entry) == 0) {
			LOGINFO("AB zip\n");
			gui_msg(Msg(msg::kHighlight, "flash_ab_inactive=Flashing A/B zip to inactive slot: {1}")(PartitionManager.Get_Active_Slot_Display()=="A"?"B":"A"));
			// We need this so backuptool can do its magic
			bool system_mount_state = PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path());
			bool vendor_mount_state = PartitionManager.Is_Mounted_By_Path("/vendor");
			PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), true);
			PartitionManager.Mount_By_Path("/vendor", true);
			TWFunc::copy_file("/system/bin/sh", "/tmp/sh", 0755);
			mount("/tmp/sh", "/system/bin/sh", "auto", MS_BIND, NULL);
			ret_val = Run_Update_Binary(path, wipe_cache, AB_OTA_ZIP_TYPE);
			umount("/system/bin/sh");
			unlink("/tmp/sh");
			if (!vendor_mount_state)
				PartitionManager.UnMount_By_Path("/vendor", true);
			if (!system_mount_state)
				PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true);
			gui_warn("flash_ab_reboot=To flash additional zips, please reboot recovery to switch to the updated slot.");
		} else {
			ZipString binary_name("ui.xml");
			ZipEntry binary_entry;
			if (FindEntry(Zip, binary_name, &binary_entry) != 0) {
				LOGINFO("OrangeFox theme zip\n");
				ret_val = Install_Theme(path, Zip);
			} else {
				CloseArchive(Zip);
				ret_val = INSTALL_CORRUPT;
			}
		}
   }

  time(&stop);
  int total_time = (int) difftime(stop, start);
  
  if (ret_val == INSTALL_CORRUPT)
    {
        set_miui_install_status(OTA_CORRUPT, true);
        gui_err("invalid_zip_format=Invalid zip file format!");
    }
  else
  if (ret_val == INSTALL_ERROR)
     {
	set_miui_install_status(OTA_ERROR, false);
     }
  else // success - so let us see whether we need to run OTA_BAK
  {
     // if MIUI-specific features have been disabled
     if (Fox_Skip_OTA()) // yes
     {
         //LOGINFO("OrangeFox: not running the incremental OTA backup (OTA_BAK).\n");
     }  
     else // else let us proceed with the OTA stuff
     if (DataManager::GetIntValue(FOX_INCREMENTAL_OTA_FAIL) != 1)
     {
      	if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) == 1 && DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE) != 0)
      	  {
      	    if (TWinstall_Run_OTA_BAK (true)) // true, because the value of Fox_Zip_Installer_Code to be set
      	      {
	        if (Fox_OTA_Backup_Stock_Boot_Image()) // whether to create an additional backup of the stock boot image
	           {
	      		usleep(2048);
	      		string ota_folder = DataManager::GetStrValue("ota_bak_folder");
	      		usleep(2048);
	      		if (ota_folder.empty())
	      		   ota_folder = "/sdcard/Fox/OTA";
			string ota_bootimg = ota_folder + "/boot.img";		
			if (TWFunc::Path_Exists(boot_bak_img)) 
			 {
			   if (TWFunc::copy_file(boot_bak_img, ota_bootimg, 0644) == 0) 
			     {
			   	LOGINFO("OrangeFox: stock boot image extracted into the OTA directory.\n");
			     }
			   unlink(boot_bak_img.c_str());
		 	}
	           }
      	      }
      	  }
      	
      	DataManager::SetValue(FOX_METADATA_PRE_BUILD, 0);
      	DataManager::SetValue(FOX_MIUI_ZIP_TMP, 0);
      	DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 0);
      	DataManager::SetValue(FOX_LOADED_FINGERPRINT, 0);
      	DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 0);
      
     } // end of OTA stuff
    LOGINFO("Install took %i second(s).\n", total_time);
   }

   if (ret_val == INSTALL_SUCCESS)
      set_miui_install_status(OTA_SUCCESS, false);

   usleep(32);
   if (DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE) != 0) // just flashed a ROM
   {
      usleep(16);
      TWFunc::Check_OrangeFox_Overwrite_FromROM(false, path);
   }
 
  return ret_val;
}

