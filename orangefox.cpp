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
#include "twinstall/install.h"
#include "twinstall/verifier.h"
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
#include "legacy_property_service.h"
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
         if (TWFunc::Fox_Property_Set("ro.product.device", assert_device) == 0)
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
//gui_print("DEBUG: OTA: OTA_BAK - 000\n");
//int miui_tmp=DataManager::GetIntValue(FOX_MIUI_ZIP_TMP);
//int meta_tmp=DataManager::GetIntValue(FOX_METADATA_PRE_BUILD);
//gui_print("DEBUG: OTA: OTA_BAK - 000 miui_tmp=%i, and meta_tmp=%i\n", miui_tmp, meta_tmp);

      if ((DataManager::GetIntValue(FOX_MIUI_ZIP_TMP) != 0) || (DataManager::GetIntValue(FOX_METADATA_PRE_BUILD) != 0))
      {
//gui_print("DEBUG: OTA: OTA_BAK - 001\n");
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
  ZipString zip_string(filename.c_str());
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
	ZipString zip_string(source_file.c_str());
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

// ------------------- end ----------------
