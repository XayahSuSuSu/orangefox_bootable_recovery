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

	This file is neeeded because of twinstall being moved to Soong,
	such that conditions from Android.mk etc, are not being processed.
	So we use boolean functions here to do that job.
	
*/

#ifndef ORANGEFOX_HPP
#define ORANGEFOX_HPP

#define OTA_CORRUPT "INSTALL_CORRUPT"
#define OTA_ERROR "INSTALL_ERROR"
#define OTA_VERIFY_FAIL "INSTALL_VERIFY_FAILURE"
#define OTA_SUCCESS "INSTALL_SUCCESS"
#define FOX_TMP_PATH "/foxtmpfile"

// global variables
static bool zip_is_for_specific_build = false;
static bool zip_is_rom_package = false;
static bool zip_is_survival_trigger = false;
static bool support_all_block_ota = false;
const std::string boot_bak_img = "/tmp/stock_boot.img";

// global functions
bool storage_is_encrypted();
bool ors_is_active();
void set_miui_install_status(std::string install_status, bool verify);
std::string Fox_CheckForAsserts(void); // see whether asserts affect our alternate devices, and if so, which one
void Fox_ProcessAsserts(std::string assert_device); // whether to try and match asserts with our alternate devices
int TWinstall_Run_OTA_BAK (bool reportback); // run the OTA_BAK stuff

bool Fox_Support_All_OTA(); // whether to support custom ROM OTAs
bool Fox_Skip_OTA(); // whether to skip the OTA functions
bool Fox_OTA_Backup_Stock_Boot_Image(); // whether to make a separate backup of the stock boot image during OTA backups
bool Fox_Fix_OTA_Update_Manual_Flash_Error(); // whether to try and recover from a situation where people try to flash a block-based OTA zip manually
bool Fox_OTA_RES_Check_MicroSD(); // whether to check the external MicroSD (if any) for the OTA backup files
void Fox_Post_Zip_Install(const int result); // check after zip install, to see whether it is a ROM and whether to run the OrangeFox processes after flashing a ROM
bool is_comment_line(const std::string Src);
bool verify_incremental_package(std::string fingerprint, std::string metadatafp, std::string metadatadevice);
bool Fox_Skip_Treble_Compatibility_Check(void); // whether to skip the Treble compatibility checks

#ifdef USE_MINZIP
bool zip_EntryExists(ZipArchive Zip, const std::string& filename);
bool zip_ExtractEntry(ZipArchive Zip, const std::string& source_file, const std::string& target_file, mode_t mode);
#else
bool zip_EntryExists(ZipArchiveHandle Zip, const std::string& filename);
bool zip_ExtractEntry(ZipArchiveHandle Zip, const std::string& source_file, const std::string& target_file, mode_t mode);
#endif

bool Installing_ROM_Query(const std::string path, ZipArchiveHandle Zip); // check for ROM installs, and return true if we are installing a ROM
int Fox_Prepare_Update_Binary(const char *path, ZipArchiveHandle Zip); // OrangeFox extensions to Prepare_Update_Binary() 
#endif
