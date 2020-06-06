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

#include "twcommon.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"

#ifdef USE_MINZIP
#include "minzip/SysUtil.h"
#else
#include "otautil/SysUtil.h"
#include <ziparchive/zip_archive.h>
#endif
#include "zipwrap.hpp"
#ifdef USE_OLD_VERIFIER
#include "verifier24/verifier.h"
#else
#include "verifier.h"
#endif
#include "variables.h"
#include "cutils/properties.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrpDigestDriver.hpp"
#include "twrpDigest/twrpDigest.hpp"
#include "twrpDigest/twrpMD5.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/blanktimer.hpp"
#include "legacy_property_service.h"
#include "twinstall.h"
#include "installcommand.h"
extern "C"
{
#include "gui/gui.h"
}

#define AB_OTA "payload_properties.txt"
#define OTA_CORRUPT "INSTALL_CORRUPT"
#define OTA_ERROR "INSTALL_ERROR"
#define OTA_VERIFY_FAIL "INSTALL_VERIFY_FAILURE"
#define OTA_SUCCESS "INSTALL_SUCCESS"
#define FOX_TMP_PATH "/foxtmpfile"

static const char *properties_path = "/dev/__properties__";
static const char *properties_path_renamed = "/dev/__properties_kk__";
static bool legacy_props_env_initd = false;
static bool legacy_props_path_modified = false;
static bool zip_is_for_specific_build = false;
static bool zip_is_rom_package = false;
static bool zip_survival_failed = false;
static bool zip_is_survival_trigger = false;
static bool support_all_block_ota = false;
const string boot_bak_img = "/tmp/stock_boot.img";

enum zip_type
{
  UNKNOWN_ZIP_TYPE = 0,
  UPDATE_BINARY_ZIP_TYPE,
  AB_OTA_ZIP_TYPE,
  TWRP_THEME_ZIP_TYPE
};

static bool storage_is_encrypted()
{
  return DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0;
}

static bool ors_is_active()
{
  return DataManager::GetStrValue("tw_action") == "openrecoveryscript";
}

// to support pre-KitKat update-binaries that expect properties in the legacy format
static int switch_to_legacy_properties()
{
  if (!legacy_props_env_initd)
    {
      if (legacy_properties_init() != 0)
	return -1;

      char tmp[32];
      int propfd, propsz;
      legacy_get_property_workspace(&propfd, &propsz);
      sprintf(tmp, "%d,%d", dup(propfd), propsz);
      setenv("ANDROID_PROPERTY_WORKSPACE", tmp, 1);
      legacy_props_env_initd = true;
    }

  if (TWFunc::Path_Exists(properties_path))
    {
      // hide real properties so that the updater uses the envvar to find the legacy format properties
      if (rename(properties_path, properties_path_renamed) != 0)
	{
	  LOGERR("Renaming %s failed: %s\n", properties_path,
		 strerror(errno));
	  return -1;
	}
      else
	{
	  legacy_props_path_modified = true;
	}
    }

  return 0;
}

static int switch_to_new_properties()
{
  if (TWFunc::Path_Exists(properties_path_renamed))
    {
      if (rename(properties_path_renamed, properties_path) != 0)
	{
	  LOGERR("Renaming %s failed: %s\n", properties_path_renamed,
		 strerror(errno));
	  return -1;
	}
      else
	{
	  legacy_props_path_modified = false;
	}
    }

  return 0;
}

static int Install_Theme(const char *path, ZipWrap * Zip)
{
  Zip->Close();
  return INSTALL_CORRUPT;
}

static void set_miui_install_status(std::string install_status, bool verify)
{
  if (ors_is_active())
    {
      std::string last_status = "/cache/recovery/last_status";
      if (!PartitionManager.Mount_By_Path("/cache", true))
	return;
      if (!verify)
	{
	  if (zip_is_survival_trigger || zip_is_for_specific_build)
	    {
	      if (TWFunc::Path_Exists(last_status))
		unlink(last_status.c_str());

	      ofstream status;
	      status.open(last_status.c_str());
	      status << install_status;
	      status.close();
	      chmod(last_status.c_str(), 0755);
	    }
	}
      else
	{
	  if (TWFunc::Path_Exists(last_status))
	    unlink(last_status.c_str());

	  ofstream status;
	  status.open(last_status.c_str());
	  status << install_status;
	  status.close();
	  chmod(last_status.c_str(), 0755);
	}
    }
}

static bool verify_incremental_package(string fingerprint, string metadatafp,
				       string metadatadevice)
{
  if (metadatafp.size() > FOX_MIN_EXPECTED_FP_SIZE
      && fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE
      && metadatafp != fingerprint)
    return false;
  if (metadatadevice.size() >= 4
      && fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE
      && fingerprint.find(metadatadevice) == string::npos)
    return false;
  return (metadatadevice.size() >= 4
	  && metadatafp.size() > FOX_MIN_EXPECTED_FP_SIZE
	  && metadatafp.find(metadatadevice) == string::npos) ? false : true;
}

bool is_comment_line(const string Src)
{
  string str = TWFunc::trim(Src);
  return (str.front() == '#');
}

static bool Installing_ROM_Query(const string path, ZipWrap * Zip)
{
bool boot_install = false;
string str = "";
string tmp = "";
  if (!TWFunc::Path_Exists(path))
     return false;

  // full ROM or block-based OTA
  if (TWFunc::CheckWord(path, "block_image_update") || TWFunc::CheckWord(path, "block_image_recover"))
  	return true;

  // check for file-based OTA - make several checks
  usleep(1024);
  int i = 0;
  tmp = "boot.img";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("package_extract_file") != string::npos
  	&& Zip->EntryExists(tmp))
  	{
     	   i++;
     	   boot_install = true;
     	}

  usleep(1024);
  tmp = "/dev/block/bootdevice/by-name/system";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("mount") != string::npos
  	&& str.find("EMMC") != string::npos
  	&& str.find("/system") != string::npos
  	&& Zip->EntryExists("system/build.prop"))
     i++;

  usleep(1024);
  tmp = "/dev/block/bootdevice/by-name/vendor";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("mount") != string::npos
  	&& str.find("EMMC") != string::npos
  	&& str.find("/vendor") != string::npos
  	&& Zip->EntryExists("vendor/build.prop"))
     i++;

  // if all these are true, then no need to go further
  usleep(1024);
  if (i > 2)
    return true;

  // boot image flash? else return false
  if (!boot_install)
     return false;

  // continue 
  usleep(1024);
  tmp = "package_extract_dir(\"system\"";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && !is_comment_line(str))
     i++;

  usleep(1024);
  tmp = "package_extract_dir(\"vendor\"";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && !is_comment_line(str))
     i++;

  usleep(1024);
  tmp = "package_extract_file(\"firmware-update";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && !is_comment_line(str))
      i++;

  usleep(1024);
  tmp = "META-INF/com/miui/miui_update";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && !is_comment_line(str) && Zip->EntryExists(tmp))
      i++;

  if (i > 3)
      return true;

  // look for other signs of ROM installation
  usleep(1024);
  tmp = "/dev/block/bootdevice/by-name/cust";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("mount") != string::npos
  	&& str.find("EMMC") != string::npos
  	&& str.find("/cust") != string::npos)
     i++;
  
  usleep(1024);
  tmp = "/dev/block/bootdevice/by-name/cache";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("mount") != string::npos
  	&& str.find("EMMC") != string::npos
  	&& str.find("/cache") != string::npos)
     i++;
  
  usleep(1024);
  str = TWFunc::find_phrase(path, "format(");
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("EMMC") != string::npos
  	&& (str.find("/system") != string::npos || str.find("/vendor") != string::npos || str.find("/cache") != string::npos || str.find("/cust") != string::npos)
  	)
     i++;

  if (i > 3)
      return true;
 
  // more checks for old file-based ROM installers
  usleep(1024);
  if (Zip->EntryExists("system/bin/sh") && Zip->EntryExists("system/etc/hosts"))
    i++;

  if (Zip->EntryExists("system/media/bootanimation.zip") && Zip->EntryExists("system/vendor/bin/perfd"))
    i++;

  if (Zip->EntryExists("system/priv-app/Browser/Browser.apk") && Zip->EntryExists("system/priv-app/FindDevice/FindDevice.apk"))
    i++;
 
  usleep(1024);
  if (i > 3)
      return true;
  else
      return false;
}

static string CheckForAsserts(void)
{
string ret = "";
string device;
  #ifdef OF_TARGET_DEVICES
  device = TWFunc::get_assert_device(FOX_TMP_PATH);
  if (device.empty())
       return ret;

  string tmpstr = TWFunc::Exec_With_Output ("getprop ro.product.device");
  usleep(128000);

  if (tmpstr.empty() || tmpstr == "EXEC_ERROR!")
   	tmpstr = Fox_Current_Device;

  if (device == tmpstr)
       return ret;

  LOGINFO("AssertDevice=[%s] and CurrentDevice=[%s]\n", device.c_str(), tmpstr.c_str());
  std::vector <std::string> devs = TWFunc::Split_String(OF_TARGET_DEVICES, ",");
  
  string temp = "";   
  for (size_t i = 0; i < devs.size(); ++i)
    {
   	usleep(4096);
   	temp = TWFunc::removechar(devs[i], ' ');
   	// make sure we are not processing the current device
   	if (tmpstr != temp)
   	   {
   	      if (device == temp)
   	      	{
		  LOGINFO("Found AssertDevice [%s] at OF_TARGET_DEVICES # %i\n", temp.c_str(), (int)i);
   	      	  return temp;
   	    	}
   	  }
    } // for i

  #endif
  return ret;
}

static int Prepare_Update_Binary(const char *path, ZipWrap * Zip,
				 int *wipe_cache)
{
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
  int zip_has_miui_stuff = 0;
  string assert_device = "";

  zip_is_rom_package = false; 		// assume we are not installing a ROM
  zip_is_survival_trigger = false; 	// assume non-miui
  support_all_block_ota = false; 	// non-MIUI block-based OTA updates
  DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 0); // assume standard zip installer
  DataManager::SetValue(FOX_ZIP_INSTALLER_TREBLE, "0");
  DataManager::SetValue("found_fox_overwriting_rom", "0");
  
  if (!Zip->
      ExtractEntry(ASSUMED_UPDATE_BINARY_NAME, TMP_UPDATER_BINARY_PATH, 0755))
    {
      Zip->Close();
      LOGERR("Could not extract '%s'\n", ASSUMED_UPDATE_BINARY_NAME);
      return INSTALL_ERROR;
    }

  if (DataManager::GetIntValue(FOX_INSTALL_PREBUILT_ZIP) != 1)
    {
      DataManager::SetValue(FOX_METADATA_PRE_BUILD, 0);
      DataManager::SetValue(FOX_MIUI_ZIP_TMP, 0);
      DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 0);
      DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 0);
      DataManager::SetValue(FOX_LOADED_FINGERPRINT, 0);

      gui_msg("fox_install_detecting=Detecting Current Package");
      
      if (Zip->EntryExists(UPDATER_SCRIPT))
	{
	  if (Zip->ExtractEntry(UPDATER_SCRIPT, FOX_TMP_PATH, 0644))
	    {
	      if (Installing_ROM_Query(FOX_TMP_PATH, Zip))
	        {
		  zip_is_rom_package = true;
		  DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 1); // standard ROM
		  
		  // check for miui entries
		  /*
		  if (
		      TWFunc::CheckWord(FOX_TMP_PATH, "miui_update")
		   && (TWFunc::CheckWord(FOX_TMP_PATH, "firmware-update") || TWFunc::CheckWord(FOX_TMP_PATH, "ro.build.fingerprint")) // OTA
		     )
		     {
		        zip_has_miui_stuff = 1;
		     }
		    
		    */ 
	          // check for embedded recovery installs
	          if  (
	                 (TWFunc::CheckWord(FOX_TMP_PATH, "/dev/block/bootdevice/by-name/recovery")
	              && (TWFunc::CheckWord(FOX_TMP_PATH, "recovery.img") || TWFunc::CheckWord(FOX_TMP_PATH, "twrp.img"))
	              && (Zip->EntryExists("recovery.img") || Zip->EntryExists("twrp.img") || Zip->EntryExists("recovery/twrp.img") || Zip->EntryExists("recovery/recovery.img"))
	                 )
	              )
	             {
	                  DataManager::SetValue("found_fox_overwriting_rom", "1");
	                  usleep(32);
	                  TWFunc::Check_OrangeFox_Overwrite_FromROM(true, path);
	             }
		}
	      #ifdef OF_TARGET_DEVICES
	      assert_device = CheckForAsserts();
	      #endif
	      unlink(FOX_TMP_PATH);
	    }
	}
      
   // try to identify MIUI ROM installer
      if (zip_is_rom_package == true) 
         {
            if (Zip->EntryExists(FOX_MIUI_UPDATE_PATH)) // META-INF/com/miui/miui_update - if found, then this is a miui zip installer
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
                       //if (zip_has_miui_stuff == 1)
                       //{
                          zip_is_survival_trigger = true;
                          support_all_block_ota = true;
                       //}
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
      
      // is this is a MIUI installer ?
      if (zip_is_survival_trigger == true)    
	{
	  support_all_block_ota = true;
	  if ((Zip->EntryExists("system.new.dat")) || (Zip->EntryExists("system.new.dat.br")) || zip_is_rom_package == true) // we are installing a MIUI ROM
	    {
	      DataManager::SetValue(FOX_MIUI_ZIP_TMP, 1);
	      DataManager::SetValue(FOX_CALL_DEACTIVATION, 1);
	      DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 2); // MIUI ROM
	    }
	  gui_msg ("fox_install_miui_detected=- Detected MIUI Update Package");
	}
      else // this is a standard ROM installer
	{
	  if ((Zip->EntryExists("system.new.dat")) || (Zip->EntryExists("system.new.dat.br"))) // we are installing a custom ROM
	     {
	       DataManager::SetValue(FOX_CALL_DEACTIVATION, 1);
	       DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 1); // standard ROM
	       gui_msg ("fox_install_standard_detected=- Detected standard ROM zip installer");
	       #ifdef OF_SUPPORT_ALL_BLOCK_OTA_UPDATES
	       support_all_block_ota = true;
	       DataManager::SetValue(FOX_MIUI_ZIP_TMP, 1);
	       #endif
	     }
	}

   //* treble ROM ?
   if (zip_is_rom_package == true) 
    {
      if ((Zip->EntryExists("vendor.new.dat")) || (Zip->EntryExists("vendor.new.dat.br"))) // we are installing a Treble ROM
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
    
    #ifdef OF_OTA_BACKUP_STOCK_BOOT_IMAGE
    if (support_all_block_ota && Zip->EntryExists("boot.img"))
      {
      	if (Zip->ExtractEntry("boot.img", boot_bak_img, 0644))
      	   {
      	      LOGINFO("Making a temporary copy of the stock boot image.\n");
      	   }
      }
    #endif
    
    }
   //* treble

#if defined(OF_DISABLE_MIUI_SPECIFIC_FEATURES) || defined(OF_TWRP_COMPATIBILITY_MODE)
     // LOGINFO("OrangeFox: not executing incremental OTA restore (OTA_RES)\n");
#else
     if (zip_is_survival_trigger || support_all_block_ota)
	{
	  if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) != 0)
	    gui_msg
	       ("fox_incremental_ota_status_enabled=Support Incremental package status: Enabled");
	  
	  if (Zip->EntryExists(metadata_sg_path)) // META-INF/com/android/metadata is in zip
	    {
	      const string take_out_metadata = "/tmp/build.prop";
	      if (Zip->ExtractEntry(metadata_sg_path, take_out_metadata, 0644))
		{
		  string metadata_fingerprint = TWFunc::File_Property_Get(take_out_metadata, pre_build); // look for "pre-build"
		  string metadata_device = TWFunc::File_Property_Get(take_out_metadata, pre_device);  // look for "pre-device"
		  string fingerprint = TWFunc::System_Property_Get(fingerprint_property); // try to get system fingerprint - ro.build.fingerprint
		    
		  // appropriate "pre-build" entry in META-INF/com/android/metadata ? == incremental block-based OTA zip installer
		  if (metadata_fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE) 
		    {
		      gui_msg(Msg
			      ("fox_incremental_package_detected=Detected Incremental package '{1}'")
			      (path));
			      
		      if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) == 0)
		      {
		  	Zip->Close();
		  	LOGERR("Incremental OTA is not enabled. Quitting the incremental OTA update.\n");
		  	set_miui_install_status(OTA_ERROR, false);
		  	return INSTALL_ERROR; 
		      }
		      
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
			      gui_msg
				("fox_incremental_ota_compatibility_true=Incremental package is compatible.");
			      property_set(fingerprint_property.c_str(), metadata_fingerprint.c_str());
			      DataManager::SetValue(FOX_LOADED_FINGERPRINT, metadata_fingerprint);
			    }
			  else
			    {
			      set_miui_install_status(OTA_VERIFY_FAIL, false);
			      gui_err
				("fox_incremental_ota_compatibility_false=Incremental package isn't compatible with this ROM!");
			      return INSTALL_ERROR;
			    }
			}
		      else
			{
			  property_set(fingerprint_property.c_str(), metadata_fingerprint.c_str());
			}
		    
		      if (zip_is_for_specific_build)
			 {
			   #ifndef OF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR
			   if ((!ors_is_active()) && (zip_is_rom_package))
			       LOGERR("You must flash incremental OTA updates from the ROM's updater, because only the ROM can decrypt the zips.\n");
			   #endif
			 }			
		      unlink(take_out_metadata.c_str());		      
		    }
		}
	      else
		{
		  Zip->Close();
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
		#ifdef OF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR
	    	std::string cachefile = "/cache/recovery/openrecoveryscript";
	    	gui_print_color("warning", "\n\n- You tried to flash OTA zip (%s) manually. Attempting to recover the situation...\n\n", path);
	    	TWFunc::CreateNewFile(cachefile);
	    	usleep(256);
	    	if (TWFunc::Path_Exists(cachefile))
	       	   {
	    	   	TWFunc::AppendLineToFile(cachefile, "install " + zip_name);
	    	   	usleep(256);
	    	   	Zip->Close();
	    	   	usleep(256);
	    	   	gui_print_color("warning", "\n- Rebooting into OTA install mode in 10 seconds. Please wait ...\n\n");
	    	   	sleep(10);
	    	   	TWFunc::tw_reboot(rb_recovery);
	    	   	return INSTALL_ERROR;
	       	   }
		#endif
	      LOGERR("Please flash this package using the ROM's updater app!\n");
	      return INSTALL_ERROR;
	    }

	  string Boot_File = ota_location_folder + "/boot.emmc.win";
	  if (
	     (!storage_is_encrypted()) || (TWFunc::Path_Exists(Boot_File)) || (DataManager::GetIntValue("OTA_decrypted") == 1)
	     )
	    {
	      if (TWFunc::Path_Exists(Boot_File))
		{
		  gui_msg
		    ("fox_incremental_ota_res_run=Running restore process of the current OTA file");
		  DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 1);
		  PartitionManager.Set_Restore_Files(ota_location_folder);
		  if (PartitionManager.
		      Run_OTA_Survival_Restore(ota_location_folder))
		    {
		      gui_msg
			("fox_incremental_ota_res=Process OTA_RES -- done!!");
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
		  gui_err
		    ("fox_survival_does_not_exist=OTA Survival does not exist! Please flash a full ROM first!");
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
#endif // MIUI OTA
      if (Zip->EntryExists(bootloader))
	gui_msg(Msg
		(msg::kWarning,
		 "fox_zip_have_bootloader=Warning: OrangeFox detected bootloader inside of the {1}")
		(path));
    }

  if (blankTimer.isScreenOff())
    {
      if (Zip->EntryExists(AROMA_CONFIG))
	{
	  blankTimer.toggleBlank();
	  gui_changeOverlay("");
	}
    }

  // If exists, extract file_contexts from the zip file
  if (!Zip->EntryExists("file_contexts"))
    {
      Zip->Close();
      LOGINFO
	("Zip does not contain SELinux file_contexts file in its root.\n");
    }
  else
    {
      const string output_filename = "/file_contexts";
      LOGINFO
	("Zip contains SELinux file_contexts file in its root. Extracting to %s\n",
	 output_filename.c_str());
      if (!Zip->ExtractEntry("file_contexts", output_filename, 0644))
	{
	  Zip->Close();
	  set_miui_install_status(OTA_CORRUPT, false);
	  LOGERR("Could not extract '%s'\n", output_filename.c_str());
	  return INSTALL_ERROR;
	}
    }
  Zip->Close();
  #ifdef OF_TARGET_DEVICES
    if (!assert_device.empty())
       {
         string alt_cmd = "/sbin/resetprop";
         if (!TWFunc::Path_Exists(alt_cmd))
              	alt_cmd = "/sbin/setprop";
         
         if (!TWFunc::Path_Exists(alt_cmd))
         	return INSTALL_SUCCESS;
         
         if (TWFunc::Exec_Cmd (alt_cmd + " ro.product.device " + assert_device) == 0)
           {
       	     //gui_print_color("warning",
       	     LOGINFO("\nDevice name temporarily switched to \"%s\" until OrangeFox is rebooted.\n\n", assert_device.c_str());
       	     usleep (64000);
           }
       }
  #endif
  return INSTALL_SUCCESS;
}

static bool update_binary_has_legacy_properties(const char *binary)
{
  const char str_to_match[] = "ANDROID_PROPERTY_WORKSPACE";
  int len_to_match = sizeof(str_to_match) - 1;
  bool found = false;

  int fd = open(binary, O_RDONLY);
  if (fd < 0)
    {
      LOGINFO("has_legacy_properties: Could not open %s: %s!\n", binary,
	      strerror(errno));
      return false;
    }

  struct stat finfo;
  if (fstat(fd, &finfo) < 0)
    {
      LOGINFO("has_legacy_properties: Could not fstat %d: %s!\n", fd,
	      strerror(errno));
      close(fd);
      return false;
    }

  void *data = mmap(NULL, finfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED)
    {
      LOGINFO("has_legacy_properties: mmap (size=%ld) failed: %s!\n",
	      finfo.st_size, strerror(errno));
    }
  else
    {
      if (memmem(data, finfo.st_size, str_to_match, len_to_match))
	{
	  LOGINFO("has_legacy_properties: Found legacy property match!\n");
	  found = true;
	}
      munmap(data, finfo.st_size);
    }
  close(fd);

  return found;
}

static int Run_Update_Binary(const char *path, ZipWrap * Zip, int *wipe_cache,
			     zip_type ztype)
{
  int ret_val, pipe_fd[2], status, zip_verify, aroma_running;
  char buffer[1024];
  FILE *child_data;

#ifndef TW_NO_LEGACY_PROPS
  if (!update_binary_has_legacy_properties(TMP_UPDATER_BINARY_PATH))
    {
      LOGINFO("Legacy property environment not used in updater.\n");
    }
  else if (switch_to_legacy_properties() != 0)
    {				/* Set legacy properties */
      LOGERR
	("Legacy property environment did not initialize successfully. Properties may not be detected.\n");
    }
  else
    {
      LOGINFO("Legacy property environment initialized.\n");
    }
#endif

  pipe(pipe_fd);

  std::vector < std::string > args;
  if (ztype == UPDATE_BINARY_ZIP_TYPE)
    {
      ret_val = update_binary_command(path, 0, pipe_fd[1], &args);
    }
  else if (ztype == AB_OTA_ZIP_TYPE)
    {
      ret_val = abupdate_binary_command(path, Zip, 0, pipe_fd[1], &args);
    }
  else
    {
      LOGERR("Unknown zip type %i\n", ztype);
      ret_val = INSTALL_CORRUPT;
    }
  if (ret_val)
    {
      close(pipe_fd[0]);
      close(pipe_fd[1]);
      return ret_val;
    }

  // Convert the vector to a NULL-terminated char* array suitable for execv.
  const char *chr_args[args.size() + 1];
  chr_args[args.size()] = NULL;
  for (size_t i = 0; i < args.size(); i++)
    chr_args[i] = args[i].c_str();

  pid_t pid = fork();
  if (pid == 0)
    {
      close(pipe_fd[0]);
      execve(chr_args[0], const_cast < char **>(chr_args), environ);
      printf("E:Can't execute '%s': %s\n", chr_args[0], strerror(errno));
      _exit(-1);
    }
  close(pipe_fd[1]);

  aroma_running = 0;
  *wipe_cache = 0;

  DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
  child_data = fdopen(pipe_fd[0], "r");
  while (fgets(buffer, sizeof(buffer), child_data) != NULL)
    {
      char *command = strtok(buffer, " \n");
      if (command == NULL)
	{
	  continue;
	}
      else if (strcmp(command, "progress") == 0)
	{
	  char *fraction_char = strtok(NULL, " \n");
	  char *seconds_char = strtok(NULL, " \n");

	  float fraction_float = strtof(fraction_char, NULL);
	  int seconds_float = strtol(seconds_char, NULL, 10);

	  if (zip_verify)
	    DataManager::ShowProgress(fraction_float *
				      (1 - VERIFICATION_PROGRESS_FRAC),
				      seconds_float);
	  else
	    DataManager::ShowProgress(fraction_float, seconds_float);
	}
      else if (strcmp(command, "set_progress") == 0)
	{
	  char *fraction_char = strtok(NULL, " \n");
	  float fraction_float = strtof(fraction_char, NULL);
	  DataManager::_SetProgress(fraction_float);
	}
      else if (strcmp(command, "ui_print") == 0)
	{
	  char *display_value = strtok(NULL, "\n");
	  if (display_value)
	    {
	      if (strcmp(display_value, "AROMA Filemanager Finished...") == 0
		  && (aroma_running == 1))
		{
		  aroma_running = 0;
		  gui_changeOverlay("");
		  TWFunc::copy_file(Fox_aroma_cfg, Fox_sdcard_aroma_cfg,
				    0644);
		}
	      gui_print("%s", display_value);
	      if (strcmp(display_value, "(c) 2013-2015 by amarullz.com") == 0
		  && (aroma_running == 0))
		{
		  aroma_running = 1;
		  gui_changeOverlay("black_out");
		  TWFunc::copy_file(Fox_aroma_cfg, Fox_sdcard_aroma_cfg,
				    0644);
		}
	    }
	  else
	    {
	      gui_print("\n");
	    }
	}
      else if (strcmp(command, "wipe_cache") == 0)
	{
	  *wipe_cache = 1;
	}
      else if (strcmp(command, "clear_display") == 0)
	{
	  // Do nothing, not supported by TWRP
	}
      else if (strcmp(command, "log") == 0)
	{
	  printf("%s\n", strtok(NULL, "\n"));
	}
      else
	{
	  LOGERR("unknown command [%s]\n", command);
	}
    }
  fclose(child_data);

  int waitrc = TWFunc::Wait_For_Child(pid, &status, "Updater");

  // Should never happen, but in case of crash or other unexpected condition
  if (aroma_running == 1)
    {
      gui_changeOverlay("");
    }

  // if updater-script doesn't find the correct device
  if (WEXITSTATUS (status) == TW_ERROR_WRONG_DEVICE)
     {
       gui_print_color("error", "\nPossible causes of this error:\n  1. Wrong device\n  2. Wrong firmware\n  3. Corrupt zip.\n\nSearch online for \"error %i\". ",
       			TW_ERROR_WRONG_DEVICE);
       gui_print_color("error", "Check \"/tmp/recovery.log\", and look above, for the specific cause of this error.\n\n");
     }

#ifndef TW_NO_LEGACY_PROPS
  /* Unset legacy properties */
  if (legacy_props_path_modified)
    {
      if (switch_to_new_properties() != 0)
	{
	  LOGERR
	    ("Legacy property environment did not disable successfully. Legacy properties may still be in use.\n");
	}
      else
	{
	  LOGINFO("Legacy property environment disabled.\n");
	}
    }
#endif

  if (waitrc != 0)
    {
      set_miui_install_status(OTA_CORRUPT, false);
      return INSTALL_ERROR;
    }

  return INSTALL_SUCCESS;
}



int TWinstall_zip(const char *path, int *wipe_cache)
{
  int ret_val, zip_verify = 1, unmount_system = 1;;

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
      gui_msg(Msg("installing_zip=Installing zip file '{1}'") (path));
      
      if (strlen(path) < 9 || strncmp(path, "/sideload", 9) != 0)
	{
	  string digest_str;
	  string Full_Filename = path;
	  string digest_file = path;
	  string defmd5file = digest_file + ".md5sum";
	  if (TWFunc::Path_Exists(defmd5file)) 
	     {
		digest_file += ".md5sum";
	     }
	  else 
	     {
		digest_file += ".md5";
	     }
	  gui_msg("check_for_digest=Checking for Digest file...");
	  if (!TWFunc::Path_Exists(digest_file))
	    {
	      gui_msg
		("no_digest=Skipping Digest check: no Digest file found");
	    }
	  else
	    {
	      if (TWFunc::read_file(digest_file, digest_str) != 0)
		{
		  LOGERR("Skipping MD5 check: MD5 file unreadable\n");
		}
	      else
		{
		  twrpDigest *digest = new twrpMD5();
		  if (!twrpDigestDriver::
		      stream_file_to_digest(Full_Filename, digest))
		    {
		      delete digest;
		      return INSTALL_CORRUPT;
		    }
		  string digest_check = digest->return_digest_string();
		  if (digest_str == digest_check)
		    {
		      gui_msg(Msg("digest_matched=Digest matched for '{1}'.")
			      (path));
		    }
		  else
		    {
		      LOGERR
			("Aborting zip install: Digest verification failed\n");
		      set_miui_install_status(OTA_CORRUPT, true);
		      delete digest;
		      return INSTALL_CORRUPT;
		    }
		  delete digest;
		}
	    }
	}
    }

  DataManager::GetValue(TW_UNMOUNT_SYSTEM, unmount_system);

#ifndef TW_OEM_BUILD
  DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, zip_verify);
#endif

  DataManager::SetProgress(0);

  MemMapping map;
#ifdef USE_MINZIP
  if (sysMapFile(path, &map) != 0)
    {
#else
  if (!map.MapFile(path))
    {
#endif
      gui_msg(Msg(msg::kError, "fail_sysmap=Failed to map file '{1}'")
	      (path));
      return -1;
    }

  if (zip_verify)
    {
      gui_msg("verify_zip_sig=Verifying zip signature...");
#ifdef USE_OLD_VERIFIER
      ret_val = verify_file(map.addr, map.length);
#else
      std::vector < Certificate > loadedKeys;
      if (!load_keys("/res/keys", loadedKeys))
	{
	  LOGINFO("Failed to load keys");
	  gui_err("verify_zip_fail=Zip signature verification failed!");
	  set_miui_install_status(OTA_VERIFY_FAIL, true);
#ifdef USE_MINZIP
	  sysReleaseMap(&map);
#endif
	  return -1;
	}
      ret_val =
	verify_file(map.addr, map.length, loadedKeys,
		    std::bind(&DataManager::SetProgress,
			      std::placeholders::_1));
#endif
      if (ret_val != VERIFY_SUCCESS)
	{
	  LOGINFO("Zip signature verification failed: %i\n", ret_val);
	  gui_err("verify_zip_fail=Zip signature verification failed!");
	  set_miui_install_status(OTA_VERIFY_FAIL, true);
#ifdef USE_MINZIP
	  sysReleaseMap(&map);
#endif
	  return -1;
	}
      else
	{
	  gui_msg("verify_zip_done=Zip signature verified successfully.");
	}

    }

  ZipWrap Zip;
  if (!Zip.Open(path, &map))
    {
      set_miui_install_status(OTA_CORRUPT, true);
      gui_err("zip_corrupt=Zip file is corrupt!");
#ifdef USE_MINZIP
      sysReleaseMap(&map);
#endif
      return INSTALL_CORRUPT;
    }

	if (unmount_system) {
		gui_msg("unmount_system=Unmounting System...");
		if(!PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true)) {
			gui_err("unmount_system_err=Failed unmounting System");
			return -1;
		}
		unlink("/system");
		mkdir("/system", 0755);
	}

  time_t start, stop;
  time(&start);
  if (Zip.EntryExists(ASSUMED_UPDATE_BINARY_NAME))
    {
      LOGINFO("Update binary zip\n");
      // Additionally verify the compatibility of the package.
      if (!verify_package_compatibility(&Zip))
	{
	  gui_err("zip_compatible_err=Zip Treble compatibility error!");
	  Zip.Close();
#ifdef USE_MINZIP
	  sysReleaseMap(&map);
#endif
	  ret_val = INSTALL_CORRUPT;
	}
      else
	{
	  ret_val = Prepare_Update_Binary(path, &Zip, wipe_cache);
	  usleep(32);
	  if (ret_val == INSTALL_SUCCESS)
	    {
	  	TWFunc::Run_Pre_Flash_Protocol(false);
	        ret_val = Run_Update_Binary (path, &Zip, wipe_cache, UPDATE_BINARY_ZIP_TYPE);
	        usleep(32); 	
	  	TWFunc::Run_Post_Flash_Protocol();
	    }
	  else 
	   {
	      zip_survival_failed = true;
	      DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 1);
	   }  
	  if (ret_val != INSTALL_SUCCESS)
	   {
	       zip_survival_failed = true;
	       DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 1);
	   }    
	}
    }
  else
    {
      if (Zip.EntryExists(AB_OTA))
	{
		LOGINFO("AB zip\n");
		// We need this so backuptool can do its magic
		bool system_mount_state = PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path());
		bool vendor_mount_state = PartitionManager.Is_Mounted_By_Path("/vendor");
		PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), true);
		PartitionManager.Mount_By_Path("/vendor", true);
		TWFunc::Exec_Cmd("cp -f /sbin/sh /tmp/sh");
		mount("/tmp/sh", "/system/bin/sh", "auto", MS_BIND, NULL);
		ret_val = Run_Update_Binary(path, &Zip, wipe_cache, AB_OTA_ZIP_TYPE);
		umount("/system/bin/sh");
		unlink("/tmp/sh");
		if (!vendor_mount_state)
			PartitionManager.UnMount_By_Path("/vendor", true);
		if (!system_mount_state)
			PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), true);
	}
      else
	{
	  if (Zip.EntryExists("orangefox.prop"))
	    {
	      LOGINFO("OrangeFox Update\n");
	      ret_val = Install_Theme(path, &Zip);
	    }
	  else
	    {
	      Zip.Close();
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
#if defined(OF_DISABLE_MIUI_SPECIFIC_FEATURES) || defined(OF_TWRP_COMPATIBILITY_MODE)
   // LOGINFO("OrangeFox: not running the incremental OTA backup (OTA_BAK).\n");
#else    
   else  
   if (DataManager::GetIntValue(FOX_INCREMENTAL_OTA_FAIL) != 1)
     {
      	if (DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) == 1 && DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE) != 0)
      	  {
      	    if (TWinstall_Run_OTA_BAK (true)) // true, because the value of Fox_Zip_Installer_Code to be set
      	      {
	        #ifdef OF_OTA_BACKUP_STOCK_BOOT_IMAGE
	      	usleep(2048);
	      	string ota_folder = DataManager::GetStrValue("ota_bak_folder");
	      	usleep(2048);
	      	if (ota_folder.empty())
	      	   ota_folder = "/sdcard/Fox/OTA";
		string ota_bootimg = ota_folder + "/boot.img";
		
		/*
		string str_path = path;
		string cmd = "unzip -o " + str_path + " boot.img -p > " + ota_bootimg;
		TWFunc::Exec_Cmd(cmd);
		*/

		if (TWFunc::Path_Exists(boot_bak_img))
		{
		   if (TWFunc::copy_file(boot_bak_img, ota_bootimg, 0644) == 0)
		     {
		   	LOGINFO("OrangeFox: stock boot image extracted into the OTA directory.\n");
		     }
		   unlink(boot_bak_img.c_str());
		}
	        #endif
      	      }
      	  }
      	
      	DataManager::SetValue(FOX_METADATA_PRE_BUILD, 0);
      	DataManager::SetValue(FOX_MIUI_ZIP_TMP, 0);
      	DataManager::SetValue(FOX_INCREMENTAL_OTA_FAIL, 0);
      	DataManager::SetValue(FOX_LOADED_FINGERPRINT, 0);
      	DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 0);
      
      	LOGINFO("Install took %i second(s).\n", total_time);
     }
#endif // OF_DISABLE_MIUI_SPECIFIC_FEATURES

   if (ret_val == INSTALL_SUCCESS)
      set_miui_install_status(OTA_SUCCESS, false);

#ifdef USE_MINZIP
  sysReleaseMap(&map);
#endif

   usleep(32);
   if (DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE) != 0) // just flashed a ROM
   {
      usleep(16);
      TWFunc::Check_OrangeFox_Overwrite_FromROM(false, path);
   }
 
  return ret_val;
}

int TWinstall_Run_OTA_BAK (bool reportback) 
{
int result = 0;
#ifdef OF_VANILLA_BUILD
   LOGINFO("- OrangeFox: DEBUG: skipping the OTA_BAK process...\n");
   return result;
#endif
      if ((DataManager::GetIntValue(FOX_MIUI_ZIP_TMP) != 0) || (DataManager::GetIntValue(FOX_METADATA_PRE_BUILD) != 0))
      {
	  string ota_folder, ota_backup, loadedfp;
	  DataManager::GetValue(FOX_SURVIVAL_FOLDER_VAR, ota_folder);
	  DataManager::GetValue(FOX_SURVIVAL_BACKUP_NAME, ota_backup);
	  DataManager::GetValue(FOX_LOADED_FINGERPRINT, loadedfp);
	  ota_folder += "/" + ota_backup;
	  string ota_info = ota_folder + Fox_OTA_info;
	  if (TWFunc::Verify_Loaded_OTA_Signature(loadedfp, ota_folder))
	    {
	        gui_msg
		("fox_incremental_ota_bak_skip=Detected OTA survival with the same ID - leaving");
		return result;
	    }
	    
	    if (TWFunc::Path_Exists(ota_folder))
		    TWFunc::removeDir(ota_folder, false);

	    DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 1);
	    gui_msg
		("fox_incremental_ota_bak_run=Starting OTA_BAK process...");

	    if (PartitionManager.Run_OTA_Survival_Backup(false))
	        {
	           result = 1;
	           DataManager::SetValue("ota_bak_folder", ota_folder);
	           set_miui_install_status(OTA_SUCCESS, false);    
	           gui_msg("fox_incremental_ota_bak=Process OTA_BAK --- done!");
	           Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
	           usleep(1024);
	           if (reportback)
	           {
	           	Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
	           	// MIUI: 2, 3, 22, 23
	           	if (Fox_Zip_Installer_Code == 22) // Treble MIUI
	               	   DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 23);
	           	else
	           	if (Fox_Zip_Installer_Code == 2) // non-Treble MIUI
	               	   DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 3);
	           	else
	           	// custom: 1, 11, 12, 13
	           	if (Fox_Zip_Installer_Code == 11) // Treble Custom
	               	   DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 12);
	           	else
	               	   DataManager::SetValue(FOX_ZIP_INSTALLER_CODE, 13); // non-Treble custom
			usleep(1024);
	           	Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
	           	LOGINFO("OTA_BAK status: [code=%i]\n", Fox_Zip_Installer_Code);
	           }
	           else
	           {
	           	DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 0);
	           }
	        }
	    else
	        {
		   set_miui_install_status(OTA_ERROR, false);
	           gui_print("Process OTA_BAK --- FAILED!\n");
	           LOGERR("OTA_BAK: Unable to finish OTA_BAK!\n");
	        }  

	    if ((TWFunc::Path_Exists(ota_folder)) && (!TWFunc::Path_Exists(ota_info)))
		{
		   TWFunc::create_fingerprint_file(ota_info, loadedfp);
		}
      } // FOX_MIUI_ZIP_TMP
      return result;
}
