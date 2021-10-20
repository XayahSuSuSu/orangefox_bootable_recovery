/*
	Copyright (C) 2020-2021 OrangeFox Recovery Project
	This file is part of the OrangeFox Recovery Project.
	
	OrangeFox is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	
	OrangeFox is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with OrangeFox.  If not, see <http://www.gnu.org/licenses/>.
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
#include "install/include/install/install.h"
#include "install/include/install/verifier.h"
#include "variables.h"
#include "data.hpp"
#include "partitions.hpp"
#include "twrpDigestDriver.hpp"
#include "twrpDigest/twrpDigest.hpp"
#include "twrpDigest/twrpMD5.hpp"
#include "twrp-functions.hpp"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/blanktimer.hpp"
#include "orangefox.hpp"
// #include "legacy_property_service.h"

#include "twinstall.h"
#include "installcommand.h"
extern "C" {
	#include "gui/gui.h"
}

bool storage_is_encrypted()
{
  return DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0;
}

bool ors_is_active()
{
  return DataManager::GetStrValue("tw_action") == "openrecoveryscript";
}


string Fox_CheckForAsserts(void)
{
string ret = "";
#ifdef OF_TARGET_DEVICES
string device;
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

bool Fox_Support_All_OTA()
{
  #ifdef OF_SUPPORT_ALL_BLOCK_OTA_UPDATES
  DataManager::SetValue(FOX_MIUI_ZIP_TMP, 1);
  return true;
  #else
  return false;
  #endif
}

void Fox_ProcessAsserts(string assert_device)
{
  #ifdef OF_TARGET_DEVICES
    if (!assert_device.empty())
       {
         if (TWFunc::Fox_Property_Set("ro.product.device", assert_device))
           {
       	     //gui_print_color("warning",
       	     LOGINFO("\nDevice name temporarily switched to \"%s\" until OrangeFox is rebooted.\n\n", assert_device.c_str());
       	     usleep (64000);
           }
       }
  #endif
}

void set_miui_install_status(std::string install_status, bool verify)
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

bool is_comment_line(const string Src)
{
  string str = TWFunc::trim(Src);
  return (str.front() == '#');
}

bool Fox_Skip_OTA() 
{
   #if defined(OF_DISABLE_MIUI_SPECIFIC_FEATURES) || defined(OF_TWRP_COMPATIBILITY_MODE)
   return true;
   #else
   return false;
   #endif
}

bool Fox_OTA_Backup_Stock_Boot_Image()
{
   #ifdef OF_OTA_BACKUP_STOCK_BOOT_IMAGE
   return true;
   #else
   return false;
   #endif
}

bool Fox_Fix_OTA_Update_Manual_Flash_Error()
{
   #ifdef OF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR
   return true;
   #else
   return false;
   #endif
}

bool Fox_OTA_RES_Check_MicroSD()
{
   #ifdef OF_OTA_RES_CHECK_MICROSD
   return true;
   #else
   return false;
   #endif
}

bool verify_incremental_package(string fingerprint, string metadatafp, string metadatadevice)
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
	  if (TWFunc::Verify_Loaded_OTA_Signature(loadedfp, ota_folder)) {
	        gui_msg ("fox_incremental_ota_bak_skip=Detected OTA survival with the same ID - leaving");
		return result;
	    }
	    
	    if (TWFunc::Path_Exists(ota_folder))
		    TWFunc::removeDir(ota_folder, false);

	    DataManager::SetValue(FOX_RUN_SURVIVAL_BACKUP, 1);
	    gui_msg ("fox_incremental_ota_bak_run=Starting OTA_BAK process...");

	    if (PartitionManager.Run_OTA_Survival_Backup(false)) {
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

#ifdef USE_MINZIP
bool zip_EntryExists(ZipArchive Zip, const string& filename) 
{
  const ZipEntry* file_location = mzFindZipEntry(&Zip, filename.c_str());
  if (file_location != NULL)
  	return true;
  return false;
}
#else
bool zip_EntryExists(ZipArchiveHandle Zip, const string& filename) 
{
  std::string_view zip_string(filename.c_str());
  ZipEntry file_entry;
  if (FindEntry(Zip, zip_string, &file_entry) != 0)
	return false;
  return true;
}
#endif

#ifdef USE_MINZIP
bool zip_ExtractEntry(ZipArchive Zip, const string& source_file, const string& target_file, mode_t mode)
#else
bool zip_ExtractEntry(ZipArchiveHandle Zip, const string& source_file, const string& target_file, mode_t mode)
#endif
{
	unlink(target_file.c_str());
	int fd = creat(target_file.c_str(), mode);
	if (fd == -1) {
		return false;
	}
#ifdef USE_MINZIP
	const ZipEntry* file_entry = mzFindZipEntry(&Zip, source_file.c_str());
	if (file_entry == NULL) {
		printf("'%s' does not exist in zip '%s'\n", source_file.c_str(), zip_file.c_str());
		close(fd); // ??
		return false;
	}
	int ret_val = mzExtractZipEntryToFile(&Zip, file_entry, fd);
	close(fd);

	if (!ret_val) {
		printf("Could not extract '%s'\n", target_file.c_str());
		return false;
	}
#else
	std::string_view zip_string(source_file.c_str());
	ZipEntry file_entry;

	if (FindEntry(Zip, zip_string, &file_entry) != 0) {
		close(fd); // ??
		return false;
	}
	int32_t ret_val = ExtractEntryToFile(Zip, &file_entry, fd);
	close(fd);

	if (ret_val != 0) {
		printf("Could not extract '%s'\n", target_file.c_str());
		return false;
	}
#endif
	return true;
}

bool Installing_ROM_Query(const string path, ZipArchiveHandle Zip)
{
bool boot_install = false;
string str = "";
string tmp = "";
  if (!TWFunc::Path_Exists(path))
     return false;

  // full ROM or block-based OTA
  if (TWFunc::CheckWord(path, "block_image_update") || TWFunc::CheckWord(path, "block_image_recover"))
  	return true;

  // check for certain ROM files
  if ((zip_EntryExists(Zip, "system.new.dat") || zip_EntryExists(Zip, "system.new.dat.br") || zip_EntryExists(Zip, "system_ext.new.dat.br")) && zip_EntryExists(Zip, "system.transfer.list"))
     {
  	if ((zip_EntryExists(Zip, "vendor.new.dat") || zip_EntryExists(Zip, "vendor.new.dat.br")) && zip_EntryExists(Zip, "vendor.transfer.list"))
  		return true;
     }

  // check for file-based OTA - make several checks
  usleep(1024);
  int i = 0;
  tmp = "boot.img";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("package_extract_file") != string::npos
  	&& zip_EntryExists(Zip, tmp))
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
  	&& zip_EntryExists(Zip, "system/build.prop"))
     i++;

  usleep(1024);
  tmp = "/dev/block/bootdevice/by-name/vendor";
  str = TWFunc::find_phrase(path, tmp);
  if (!str.empty() && (!is_comment_line(str))
  	&& str.find("mount") != string::npos
  	&& str.find("EMMC") != string::npos
  	&& str.find("/vendor") != string::npos
  	&& zip_EntryExists(Zip, "vendor/build.prop"))
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
  if (!str.empty() && !is_comment_line(str) && zip_EntryExists(Zip, tmp))
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
  if (zip_EntryExists(Zip, "system/bin/sh") && zip_EntryExists(Zip, "system/etc/hosts"))
    i++;

  if (zip_EntryExists(Zip, "system/media/bootanimation.zip") && zip_EntryExists(Zip, "system/vendor/bin/perfd"))
    i++;

  if (zip_EntryExists(Zip, "system/priv-app/Browser/Browser.apk") && zip_EntryExists(Zip, "system/priv-app/FindDevice/FindDevice.apk"))
    i++;
 
  usleep(1024);
  if (i > 3)
      return true;
  else
      return false;
}

int Fox_Prepare_Update_Binary(const char *path, ZipArchiveHandle Zip) 
{
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
            else
            if (zip_EntryExists(Zip, FOX_MIUI_UPDATE_PATH_EU)) // META-INF/com/xiaomieu/xiaomieu.sh - if found, then this is a xiaomi.eu zip installer
              {
                zip_is_survival_trigger = true;
                support_all_block_ota = true;
                LOGINFO("OrangeFox: Detected xiaomi.eu file [%s]\n", FOX_MIUI_UPDATE_PATH_EU);
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
		        // found on /sdcard1/Fox/OTA/ - try to copy it to /sdcard/Fox/OTA/
		        if (!TWFunc::Path_Exists("/sdcard/Fox/OTA"))
		           TWFunc::Recursive_Mkdir("/sdcard/Fox/OTA/", false);

		        if (TWFunc::copy_file(atmp_ota, Boot_File, 0644) == 0) {
		    	    gui_print("- OTA backup found in /sdcard1/Fox/OTA/ - copied it to /sdcard/Fox/OTA/ ...\n");
		    	    #ifdef OF_INCREMENTAL_OTA_BACKUP_SUPER
		    	    if (TWFunc::Path_Exists("/sdcard1/Fox/OTA/super.emmc.win") && !TWFunc::Path_Exists("/sdcard/Fox/OTA/super.emmc.win")) {
		    	   	TWFunc::copy_file("/sdcard1/Fox/OTA/super.emmc.win", "/sdcard/Fox/OTA/super.emmc.win", 0644);
		    	    }
		    	    #endif
		        } else { // copy did not succeed - use the /sdcard1/ location
		    	   Boot_File = atmp_ota;
		    	   ota_location_folder = "/sdcard1/Fox/OTA";
		    	   gui_print("- OTA backup found in /sdcard1/Fox/OTA/ - trying that instead.\n");
		    	}
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
    
    } // (DataManager::GetIntValue(FOX_INSTALL_PREBUILT_ZIP) != 1)

  if (blankTimer.isScreenOff())
    {
      if (zip_EntryExists(Zip, AROMA_CONFIG))
	{
	  blankTimer.toggleBlank();
	  gui_changeOverlay("");
	}
    }

    Fox_ProcessAsserts(assert_device);

    return INSTALL_SUCCESS;
}

void Fox_Post_Zip_Install(const int result)
{
   if (result != INSTALL_SUCCESS)
   	return;

   Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
   if (Fox_Zip_Installer_Code != 0) // a ROM was installed
     {
         usleep(16384);
         TWFunc::Deactivation_Process();
         DataManager::SetValue(FOX_CALL_DEACTIVATION, 0);
         
         usleep(16384);
         TWFunc::Patch_AVB20(false);
         usleep(16384);

    	 // Run any custom script after ROM flashing
    	 TWFunc::MIUI_ROM_SetProperty(Fox_Zip_Installer_Code);
     	 TWFunc::RunFoxScript("/sbin/afterromflash.sh");
         usleep(16384);
         
         //
         PartitionManager.Update_System_Details();
     }
}

bool Fox_Skip_Treble_Compatibility_Check()
{
   #ifdef OF_NO_TREBLE_COMPATIBILITY_CHECK
   return true;
   #else
   return false;
   #endif
}

//
