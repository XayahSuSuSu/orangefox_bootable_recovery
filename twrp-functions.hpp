/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

  	Copyright (C) 2018-2022 OrangeFox Recovery Project
 	This file is part of the OrangeFox Recovery Project
 
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

#ifndef _TWRPFUNCTIONS_HPP
#define _TWRPFUNCTIONS_HPP

#include <string>
#include <vector>

#include "twrpDigest/twrpDigest.hpp"

#ifndef BUILD_TWRPTAR_MAIN
#include "partitions.hpp"
#endif

#ifdef USE_FSCRYPT
#include "fscrypt_policy.h"
#endif

//#include "crypto/ext4crypt/ext4crypt_tar.h"

using namespace std;

#define CACHE_LOGS_DIR "/cache/"	// For devices with a dedicated cache partition
#define DATA_LOGS_DIR "/data/"		// For devices that do not have a dedicated cache partition
#define PERSIST_LOGS_DIR "/persist/"	// For devices with neither cache or dedicated data partition

typedef enum
{
	rb_current = 0,
	rb_system,
	rb_recovery,
	rb_poweroff,
	rb_bootloader,
	rb_download,
	rb_edl,
	rb_fastboot
} RebootCommand;

enum Archive_Type {
	UNCOMPRESSED = 0,
	COMPRESSED,
	ENCRYPTED,
	COMPRESSED_ENCRYPTED
};

// Partition class
class TWFunc
{
public:
	static string ConvertTime(time_t time);                            	    // Convert time_t to string
	static string Get_Root_Path(const string& Path);                            // Trims any trailing folders or filenames from the path, also adds a leading / if not present
	static string Get_Path(const string& Path);                                 // Trims everything after the last / in the string
	static string Get_Filename(const string& Path);                             // Trims the path off of a filename
	static string Exec_With_Output(const string &cmd);			    // Run a command & capture the output

	static int Exec_Cmd(const string& cmd, string &result, bool combine_stderr);     //execute a command and return the result as a string by reference, set combined_stderror to add stderr
	static int Exec_Cmd(const string& cmd, string &result);                     //execute a command and return the result as a string by reference
	static int Exec_Cmd(const string& cmd, bool Show_Errors = true);                   //execute a command, displays an error to the GUI if Show_Errors is true, Show_Errors is true by default
	static int Wait_For_Child(pid_t pid, int *status, string Child_Name, bool Show_Errors = true); // Waits for pid to exit and checks exit status, displays an error to the GUI if Show_Errors is true which is the default
	static int Wait_For_Child_Timeout(pid_t pid, int *status, const string& Child_Name, int timeout); // Waits for a pid to exit until the timeout is hit. If timeout is hit, kill the chilld.
	static bool Path_Exists(string Path);                                       // Returns true if the path exists
	static bool Is_SymLink(string Path);                                        // Returns true if the path exists and is a symbolic link	
	static Archive_Type Get_File_Type(string fn);                               // Determines file type, 0 for unknown, 1 for gzip, 2 for OAES encrypted
	static int Try_Decrypting_File(string fn, string password); 		    // -1 for some error, 0 for failed to decrypt, 1 for decrypted, 3 for decrypted and found gzip format
	static unsigned long Get_File_Size(const string& Path);                     // Returns the size of a file
	static std::string Remove_Trailing_Slashes(const std::string& path, bool leaveLast = false); // Normalizes the path, e.g /data//media/ -> /data/media
	static std::string Remove_Beginning_Slash(const std::string& path);         // Remove the beginning slash of a path
	static void Strip_Quotes(char* &str);                                       // Remove leading & trailing double-quotes from a string
	static vector<string> split_string(const string &in, char del, bool skip_empty);
	static timespec timespec_diff(timespec& start, timespec& end);	            // Return a diff for 2 times
	static int32_t timespec_diff_ms(timespec& start, timespec& end);            // Returns diff in ms
	static bool Wait_For_File(const string& path, std::chrono::nanoseconds timeout); // Wait For File, True is success, False is timeout;
	static bool Wait_For_Battery(std::chrono::nanoseconds timeout);             // Wait For /sys/class/power_supply/battery or TW_CUSTOM_BATTERY_PATH, True is success, False is timeout;

#ifndef BUILD_TWRPTAR_MAIN
	static void install_htc_dumlock(void);                                      // Installs HTC Dumlock
	static void Write_MIUI_Install_Status(std::string install_status, bool verify);                                // Write last install status in to the /cache/recovery/last_status
	static void Replace_Word_In_File(string file_path, string search, string word); // Replace string in file
	static void Replace_Word_In_File(string file_path, string search); // Remove string from file	
	static void Remove_Word_From_File(string file_path, string search); // Remove string from file	
	static void Set_New_Ramdisk_Property(std::string file_path, std::string prop, bool enable); // Set new property for default.prop in unpacked ramdisk
	static bool PackRepackImage_MagiskBoot(bool do_unpack, bool is_boot);       // Unpacking/repacking process for boot/recovery images, using magiskboot
	static void htc_dumlock_restore_original_boot(void);                        // Restores the backup of boot from HTC Dumlock
	static void htc_dumlock_reflash_recovery_to_boot(void);                     // Reflashes the current recovery to boot

	static void List_Mounts();
	static void Clear_Bootloader_Message();

	static bool Repack_Image(string mount_point);
	static bool Unpack_Image(string mount_point);
	static void Read_Write_Specific_Partition(string path, string partition_name, bool backup);
	static int Get_Android_SDK_Version(void);				// Return the SDK version of the current ROM (or default to 21 (Android 5.0))
	static string Get_MagiskBoot(void);					// Return the name of the magiskboot binary that should be used for patching
	static bool Magiskboot_Repack_Patch_VBMeta();				// Return whether magiskboot v24+ repack should patch the vbmeta header with 03 (ie, replace 00 with 03)

	static void Deactivation_Process(void);                     		// Run deactivation process...
	static bool To_Skip_OrangeFox_Process(void);				// Return whether to skip the deactivation process
	static void OrangeFox_Startup(void);        				// Run StartUP code for OrangeFox
	static int Recursive_Mkdir(string Path, bool ShowErr = true);           // Recursively makes the entire path
	static void GUI_Operation_Text(string Read_Value, string Default_Text); // Updates text for display in the GUI, e.g. Backing up %partition name%
	static void GUI_Operation_Text(string Read_Value, string Partition_Name, string Default_Text); // Same as above but includes partition name
	static void Update_Log_File(void);                                      // Writes the log to last_log
	static void Update_Intent_File(string Intent);                          // Updates intent file
	static int tw_reboot(RebootCommand command);                            // Prepares the device for rebooting
	static void check_and_run_script(const char* script_file, const char* display_name); // checks for the existence of a script, chmods it to 755, then runs it
	static int removeDir(const string path, bool removeParent); //recursively remove a directory
	static int copy_file(string src, string dst, int mode, bool mount_paths=true); //copy file from src to dst with mode permissions
	static unsigned int Get_D_Type_From_Stat(string Path);                      // Returns a dirent dt_type value using stat instead of dirent
	static int read_file(string fn, vector<string>& results); //read from file
	static int read_file(string fn, string& results); //read from file
	static int read_file(string fn, uint64_t& results); //read from file
	static bool write_to_file(const string& fn, const string& line);             //write to file
	static bool write_to_file(const string& fn, const std::vector<string> lines); // write vector of strings line by line with newlines
	static bool Try_Decrypting_Backup(string Restore_Path, string Password); // true for success, false for failed to decrypt
	static string System_Property_Get(string Prop_Name);                // Returns value of Prop_Name from reading /system/build.prop
	static string System_Property_Get(string Prop_Name, TWPartitionManager &PartitionManager, string Mount_Point, string prop_file_name);     // Returns value of Prop_Name from reading provided prop file
  	static bool CheckWord(std::string filename, std::string search); // Check if the word exist in the txt file and then return true or false 
	static string File_Property_Get(string File_Path, string Prop_Name);                // Returns specified property value from the file
	static string Get_Current_Date(void);                               // Returns the current date in ccyy-m-dd--hh-nn-ss format
	static void Auto_Generate_Backup_Name();                            // Populates TW_BACKUP_NAME with a backup name based on current date and ro.build.display.id from /system/build.prop
	static void Fixup_Time_On_Boot(const string& time_paths = ""); // Fixes time on devices which need it (time_paths is a space separated list of paths to check for ats_* files)
	static std::vector<std::string> Split_String(const std::string& str, const std::string& delimiter, bool removeEmpty = true); // Splits string by delimiter
	static bool Create_Dir_Recursive(const std::string& path, mode_t mode = 0755, uid_t uid = -1, gid_t gid = -1);  // Create directory and it's parents, if they don't exist. mode, uid and gid are set to all _newly_ created folders. If whole path exists, do nothing.
	static int Set_Brightness(std::string brightness_value); // Well, you can read, it does what it says, passing return int from TWFunc::Write_File ;)
	static bool Toggle_MTP(bool enable);                                        // Disables MTP if enable is false and re-enables MTP if enable is true and it was enabled the last time it was toggled off
	static std::string to_string(unsigned long value); //convert ul to string
	static void SetPerformanceMode(bool mode); // support recovery.perf.mode
	static void Disable_Stock_Recovery_Replace(); // Disable stock ROMs from replacing OrangeFox with stock recovery
	static void Disable_Stock_Recovery_Replace_Func(); // Disable stock ROMs from replacing OrangeFox with stock recovery (/system must be already mounted)	
	static unsigned long long IOCTL_Get_Block_Size(const char* block_device);
	static void copy_kernel_log(string curr_storage); // Copy Kernel Log to Current Storage (PSTORE/KMSG)
	static void create_fingerprint_file(string file_path, string fingerprint); // Create new file and write in to it loaded fingerprintPSTORE/KMSG)
	static bool Verify_Incremental_Package(string fingerprint, string metadatafp, string metadatadevice); // Verify if the Incremental Package is compatible with the ROM
	static bool Verify_Loaded_OTA_Signature(std::string loadedfp, std::string ota_folder); // Verify loaded fingerprint from our OTA folder
	static bool isNumber(string strtocheck); // return true if number, false if not a number
	static int  stream_adb_backup(string &Restore_Name); // Tell ADB Backup to Stream to OrangeFox from GUI selection
	static int  Check_MIUI_Treble(void); // check whether we are running a MIUI or Treble ROM 
	static bool Fresh_Fox_Install(void); // have we just installed OrangeFox - do some stuff?
	static bool Check_OrangeFox_Overwrite_FromROM(bool WarnUser, const std::string name); // report on badly behaved ROM installers
	static bool JustInstalledMiui(void); // has a MIUI ROM just been installed?
	static bool RunStartupScript(void); // run startup script if not already run by init
	static void Welcome_Message(void); // provide the welcome message
	static void Run_Before_Reboot(void); // run this just before rebooting
	static string Fox_Property_Get(string Prop_Name); // get a recovery property that would be returned by getprop
	static bool Fox_Property_Set(const std::string Prop_Name, const std::string Value); // set a recovery property that would be set by setprop
	static bool Has_Dynamic_Partitions(void); // does the device have dynamic partitions?
	static bool Has_Virtual_AB_Partitions(void); // does the device have virtual A/B partitions?
	static void Mapper_to_BootDevice(const std::string block_device, const std::string partition_name); // provide symlinks to /dev/mapper/* for dynamic partitions
	static void Fox_Set_Current_Device_CodeName(void); // set and save the current device codename (esp. where the product.device is different from a unified codename)

	//
	static bool Fstab_Has_Encryption_Flag(string path); // does the fstab file have encryption flags?
	static void Patch_Encryption_Flags(string path); // patch the fstab's encryption flags
	static bool Fstab_Has_Verity_Flag(string path); // does the fstab file have dm-verity flags?
	static void Patch_Verity_Flags(string path); // patch the fstab's dm-verity flags
	static bool Has_Vendor_Partition(void); // does the device have a real vendor partition?
	static int  Patch_DMVerity_ForcedEncryption_Magisk(void); // patch dm-verity/forced-encryption with a script using magisk
	static void Patch_AVB20(bool silent); // patch avb 2.0 with a script using magisk
	static void UseSystemFingerprint(void); // use the system (ROM) fingerprint

	static bool Has_System_Root(void); // is this a system-as-root device?
	static int Rename_File(std::string oldname, std::string newname); // rename a file, using std strings
	static bool MIUI_ROM_SetProperty(const int code); // Are we running a MIUI ROM (old or freshly installed) - set fox property
	static bool RunFoxScript(std::string script); // execute a script and introduce a delay if the script was executed
	static bool MIUI_Is_Running(void); // Are we running a MIUI ROM (old or freshly installed) ?
	static void Dump_Current_Settings(void); // log some current settings before flashing a ROM
	static void Setup_Verity_Forced_Encryption(void); //setup dm-verity/forced-encryption build vars
	static void Reset_Clock(void); // reset the date/time to the recovery's build date/time
	static std::string get_log_dir(); // return recovery log storage directory
	static void check_selinux_support(); // print whether selinux support is enabled to console
	static int Property_Override(string Prop_Name, string Prop_Value); // Override properties (including ro. properties)
	static void Set_Sbin_Dir_Executable_Flags(void); // set the executable flags of all the files in the /sbin/ directory

#ifdef TW_INCLUDE_CRYPTO
#ifdef USE_FSCRYPT_POLICY_V1
	static bool Get_Encryption_Policy(struct fscrypt_policy_v1 &policy, std::string path); // return encryption policy for path
	static bool Set_Encryption_Policy(std::string path, struct fscrypt_policy_v1 &policy); // set encryption policy for path
#else
	static bool Get_Encryption_Policy(struct fscrypt_policy_v2 &policy, std::string path); // return encryption policy for path
	static bool Set_Encryption_Policy(std::string path, struct fscrypt_policy_v2 &policy); // set encryption policy for path
#endif
#endif

	static void CreateNewFile(string file_path); // create a new (text) file
	static void AppendLineToFile(string file_path, string line); // append a line to a text file
	static void PostWipeEncryption(void); // run after formatting data to recreate /data/media/0/ + /sdcard/Fox/logs/ automatically
	static int read_file(string fn, vector < wstring > &results);
	static string wstr_to_str(wstring str);
	static bool IsBinaryXML(const std::string filename); // return whether the file is a binary XML file
	static bool Check_Xml_Format(const std::string filename); // Return whether a xml is in plain xml or ABX format
	static bool abx_to_xml(const std::string path, std::string &result); // could we convert abx to xml (if so, return the full path to the converted file)
	static std::string abx_to_xml_string(const std::string path); // convert abx to xml and return the full path to the converted or empty string on error

	// string functions
	static string lowercase(const string src); /* convert string to lowercase */
	static string uppercase (const string src); /* convert string to uppercase */
	static int pos (const string subs, const string str); /* find the position of "subs" in "str" (or -1 if not found) */
	static string ltrim(string str, const string chars = "\t\n\v\f\r "); /* trim leading character(s) from string */
	static string rtrim(string str, const string chars = "\t\n\v\f\r "); /* trim trailing character(s) from string */
	static string trim(string str, const string chars = "\t\n\v\f\r "); /* trim both leading and leading character(s) from string */
	static int DeleteFromIndex(string &Str, int Index, int Size); /* delete "Size" number of characters from string, starting at Index */
	static string DeleteBefore(const string Str, const string marker, bool removemarker); /* Delete all characters before "marker" from a string */
	static string DeleteAfter(const string Str, const string marker); /* Delete all characters after "marker" from a string */
	static string find_phrase(string filename, string search); /* search for a phrase within a text file, and return the contents of the first line that has it */
	static string get_assert_device(const string filename); /* find out which device an "assert" with an ro.product.device statement wants */
	static string removechar(const string src, const char chars); /* delete all occurrences of a char from a string */

	/* convert string to number, with default value in case of error */
	static int string_to_int(string String, int def_value);
	static long string_to_long(string String, long def_value);
	static uint64_t string_to_long(string String, uint64_t def_value);
	static string sdknum_to_text(int sdk);
	static string Check_For_TwrpFolder();

private:
	static void Copy_Log(string Source, string Destination);
	static string Load_File(string extension);
	static bool Patch_Forced_Encryption(void);
    	static bool Patch_DM_Verity(void);
    	static void PrepareToFinish(void); // call this only when we are about to shutdown or reboot
    	static bool DontPatchBootImage(void); // return true to avoid patching the boot image
    	static bool Check_OrangeFox_Overwrite_FromROM_Trigger(const std::string name);
};

extern int Log_Offset;
#else
};
#endif // ndef BUILD_TWRPTAR_MAIN

#endif // _TWRPFUNCTIONS_HPP
