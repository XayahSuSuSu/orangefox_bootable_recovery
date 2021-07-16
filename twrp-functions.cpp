/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	Copyright (C) 2018-2021 OrangeFox Recovery Project
	This file is part of the OrangeFox Recovery Project.

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

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <ctime>
#include <locale>
#include <codecvt>
#include <selinux/label.h>
#include <android-base/properties.h>

#include "twrp-functions.hpp"
#include "twcommon.h"
#include "gui/gui.hpp"
#ifndef BUILD_TWRPTAR_MAIN
#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"
#include "bootloader_message/include/bootloader_message/bootloader_message.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include <sys/reboot.h>
#endif // ndef BUILD_TWRPTAR_MAIN
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
#include "openaes/inc/oaes_lib.h"
#endif
#include "set_metadata.h"
#include "twinstall.h"
#include "gui/pages.hpp"

extern "C"
{
#include "libcrecovery/common.h"
}

#ifdef TW_INCLUDE_LIBRESETPROP
    #include <resetprop.h>
#endif

struct selabel_handle *selinux_handle;

// Globals
static string tmp = Fox_tmp_dir; // "/tmp/orangefox/"
static string split_img = tmp + "/split_img";
static string ramdisk = tmp + "/ramdisk";
static string tmp_boot = tmp + "/boot.img";
static string fstab1 = PartitionManager.Get_Android_Root_Path() + "/vendor/etc"; // /system/vendor/etc
static string Internal_SD = PartitionManager.Get_Internal_Storage_Path();
static string fstab2 = "/vendor/etc";
static string exec_error_str = "EXEC_ERROR!";
static string popen_error_str = "popen error!";
int Fox_Current_ROM_IsTreble = 0;
int ROM_IsRealTreble = 0;
int New_Fox_Installation = 0;
int OrangeFox_Startup_Executed = 0;
int Fox_Has_Welcomed = 0;
string Fox_Current_ROM = "";

/* is this an A/B device? */
static bool Is_AB_Device() 
{
  #if defined(AB_OTA_UPDATER) || defined(OF_AB_DEVICE)
     return true;
  #endif
  string s = TWFunc::Fox_Property_Get("ro.boot.slot_suffix");
  string u = TWFunc::Fox_Property_Get("ro.build.ab_update");
  return (!s.empty() && u == "true");
}

/* Get the display ID of the installed ROM */
static string GetInstalledRom(void)
{
   if (Fox_Current_ROM != "")
    {
      return Fox_Current_ROM;
    }
   
   string s = TWFunc::System_Property_Get ("ro.build.display.id");
   if (s.empty())
   {
      string prop1 = PartitionManager.Get_Android_Root_Path() + "/build.prop";
      string prop2 = PartitionManager.Get_Android_Root_Path() + "/system/build.prop";
      if ((!TWFunc::Path_Exists(prop1)) && (!TWFunc::Path_Exists(prop2)))
         return s;
      s = TWFunc::System_Property_Get ("ro.build.id");
      if (s.empty())
         s = TWFunc::System_Property_Get ("ro.build.flavor");
      if (s.empty())
         s = TWFunc::System_Property_Get ("ro.build.description");
   }
   return s;
}

/* Get the value of a named variable from the prop file */
static string Get_Property (const string propname)
{
   string ret = TWFunc::Exec_With_Output ("getprop " + propname);
   if (ret == exec_error_str)
       return "";
   else
      return ret;//(Trim_Trailing_NewLine (ret));
}

/* remove trailing newline from string */
static string Trim_Trailing_NewLine (const string src)
{
   string ret = src;
   ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());   
   return ret;
}

/* is this a real treble device? (else, treble is emulated via /cust) */
static bool Is_Real_Treble(void)
{
   if (ROM_IsRealTreble == 1)
   {
      return true;
   }
   else
   {
      if (Get_Property ("orangefox.realtreble.rom") == "1" || TWFunc::Has_Vendor_Partition())
        {
           ROM_IsRealTreble = 1;
           return true;
        }
      else 
           return false;
   }
}

/* Are we running a Treble ROM (old or freshly installed) ? */
static bool Treble_Is_Running(void)
{ 
   int treble = DataManager::GetIntValue(FOX_ZIP_INSTALLER_TREBLE);
   if (Fox_Current_ROM_IsTreble == 1 || treble == 1 || ROM_IsRealTreble == 1 || Is_Real_Treble())
      return true;
   else
      return false; 
}

/* Return whether the device's storage is encrypted */
static bool StorageIsEncrypted(void)
{
  return (PartitionManager.Storage_Is_Encrypted());
}

std::string strReturnCurrentTime()
{
  time_t rawtime;
  struct tm * timeinfo;
  char buffer[80];

  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%Y%m%d_%H%M%S",timeinfo);
  std::string str(buffer);
  return str;
}

int TWFunc::string_to_int(string String, int def_value)
{
int tmp;
  if ((istringstream(String) >> tmp)) 
      return tmp;
  else
      return def_value;
}

long TWFunc::string_to_long(string String, long def_value)
{
long tmp;
  if ((istringstream(String) >> tmp)) 
      return tmp;
  else
      return def_value;
}

uint64_t TWFunc::string_to_long(string String, uint64_t def_value)
{
uint64_t tmp;
  if ((istringstream(String) >> tmp)) 
      return tmp;
  else
      return def_value;
}

/* return whether there is a real vendor partition */
bool TWFunc::Has_Vendor_Partition(void)
{
   if (TWFunc::Path_Exists ("/dev/block/bootdevice/by-name/vendor"))
       return true;
   else
      return false;
}

/* run startup script, if not already run by init */
bool TWFunc::RunStartupScript(void)
{
string tprop = Get_Property("orangefox.postinit.status");
bool i = Path_Exists(orangefox_cfg);
   
   if (i == true || tprop == "1")
      {
         LOGINFO("DEBUG: OrangeFox: the startup script has been executed.\n");
         return false;
      }
   
   LOGINFO("DEBUG: OrangeFox: running the startup script...\n");
   Exec_Cmd(FOX_STARTUP_SCRIPT);
   //Reload_Dynamic_Fstab("/etc/logical.fstab");
   return true;
}

bool TWFunc::MIUI_ROM_SetProperty(const int code)
{
string res = "0";
bool ret = false;
	
    if (code != 0) // whether a ROM was installed, and it is MIUI
      {
    	ret = (code == 2 || code == 3 || code == 22 || code == 23);
      }
    else // check for MIUI - running or freshly installed
      {
    	ret = TWFunc::MIUI_Is_Running();
      }

    if (ret)
       res = "1";

    Fox_Property_Set("orangefox.miui.rom", res);
    return ret;
}

bool TWFunc::RunFoxScript(std::string script)
{
    if (!Path_Exists(script))
       return false;

    chmod(script.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    TWFunc::Exec_Cmd(script);
    usleep(500000);
    return true;
}

/* function to run just before every reboot */
void TWFunc::Run_Before_Reboot(void)
{
    // AVB20
    Patch_AVB20(true);
    usleep(4096);

    // Run any custom script before rebooting
    TWFunc::MIUI_ROM_SetProperty(0);
    TWFunc::RunFoxScript("/system/bin/beforereboot.sh");

    // logs & stuff
    if (!Path_Exists(Fox_Logs_Dir))
       {
	  TWFunc::Recursive_Mkdir(Fox_Logs_Dir, false);
       }

    //[f/d] release info json for app
    string Fox_Current_Device = DataManager::GetStrValue(FOX_COMPATIBILITY_DEVICE);
    string build_date = DataManager::GetStrValue("FOX_BUILD_DATE_REAL");
    TWFunc::write_to_file(Fox_Logs_Dir + "/releaseinfo.json", "{\"codename\":\""+Fox_Current_Device+
      "\", \"type\":\""+ FOX_BUILD_TYPE +"\",\"version\":\""+ FOX_BUILD +"\",\"commit\":\""+
      FOX_CURRENT_DEV_STR+"\",\"date\":\""+build_date+"\"}");
    
    copy_file("/tmp/recovery.log", Fox_Logs_Dir + "/lastrecoverylog.log", 0644);

#ifdef OF_DONT_KEEP_LOG_HISTORY
    return;
#endif

    struct timeval tv;
    std::string log_file = "/recovery";
    if (gettimeofday(&tv, NULL) == 0)
     {
        std::string tmp = strReturnCurrentTime();
        log_file = log_file + "_" + tmp + ".log";
     }
   else
     {
         log_file = log_file + "_undated.log";
     }

   log_file = Fox_Logs_Dir + log_file;
   copy_file("/tmp/recovery.log", log_file, 0644);
   if (Path_Exists(Fox_Bin_Dir + "/pigz"))
     {
        string cmd = Fox_Bin_Dir + "/pigz -K --best " + log_file;
        Exec_Cmd (cmd);
     }
}

/* Execute a command */
int TWFunc::Exec_Cmd(const string& cmd, string &result) {
	FILE* exec;
	char buffer[130];
	int ret = 0;
	exec = __popen(cmd.c_str(), "r");
	if (!exec) return -1;
	while (!feof(exec)) {
		if (fgets(buffer, 128, exec) != NULL) {
			result += buffer;
		}
	}
	ret = __pclose(exec);
	return ret;
}

int TWFunc::Exec_Cmd(const string& cmd, bool Show_Errors) {
	pid_t pid;
	int status;
	string tmp = "/sbin/sh";
	if (!Path_Exists(tmp))
	   tmp = "/system/bin/sh";
	
	switch(pid = fork())
	{
		case -1:
			LOGERR("Exec_Cmd(): vfork failed: %d!\n", errno);
			return -1;
		case 0: // child
			execl(tmp.c_str(), "sh", "-c", cmd.c_str(), NULL);
			_exit(127);
			break;
		default:
		{
			if (TWFunc::Wait_For_Child(pid, &status, cmd, Show_Errors) != 0)
				return -1;
			else
				return 0;
		}
	}
}

int TWFunc::Exec_Cmd(const string& cmd, string &result, bool combine_stderr) {
	FILE* exec;
	char buffer[130];
	int ret = 0;
	std::string popen_cmd = cmd;
	if (combine_stderr)
		popen_cmd = cmd + " 2>&1";
	exec = __popen(popen_cmd.c_str(), "r");

	while (!feof(exec)) {
		if (fgets(buffer, 128, exec) != NULL) {
			result += buffer;
		}
	}
	ret = __pclose(exec);
	return ret;
}

// Returns "file.name" from a full /path/to/file.name
string TWFunc::Get_Filename(const string & Path)
{
  size_t pos = Path.find_last_of("/");
  if (pos != string::npos)
    {
      string Filename;
      Filename = Path.substr(pos + 1, Path.size() - pos - 1);
      return Filename;
    }
  else
    return Path;
}

// Returns "/path/to/" from a full /path/to/file.name
string TWFunc::Get_Path(const string & Path)
{
  size_t pos = Path.find_last_of("/");
  if (pos != string::npos)
    {
      string Pathonly;
      Pathonly = Path.substr(0, pos + 1);
      return Pathonly;
    }
  else
    return Path;
}

/* run a command and return its output */
string TWFunc::Exec_With_Output(const string &cmd)
{
  string data;
  FILE *stream;
  const int max_buffer = 256;
  char buffer[max_buffer];
  string s = cmd + " 2>&1";

  stream = popen(s.c_str(), "r");
  if (stream)
    {
      while (!feof(stream))
	{
	  if (fgets(buffer, max_buffer, stream) != NULL)
	     data.append(buffer);
	}
      pclose(stream);
      return (Trim_Trailing_NewLine (data));
    }
 else 
    return exec_error_str;
}

int TWFunc::Wait_For_Child(pid_t pid, int *status, string Child_Name, bool Show_Errors) {
	pid_t rc_pid;

	rc_pid = waitpid(pid, status, 0);
	if (rc_pid > 0) {
		if (WIFSIGNALED(*status)) {
			if (Show_Errors)
				gui_msg(Msg(msg::kError, "pid_signal={1} process ended with signal: {2}")(Child_Name)(WTERMSIG(*status))); // Seg fault or some other non-graceful termination
			return -1;
		} else if (WEXITSTATUS(*status) == 0) {
			//LOGINFO("%s process ended with RC=%d\n", Child_Name.c_str(), WEXITSTATUS(*status)); // Success
			if (Show_Errors)
			  LOGINFO("Process ended with no errors.\n"); //[f/d] Process ended w/ no errors, why we need to сlutter up logs?
		} else {
			if (Show_Errors)
				gui_msg(Msg(msg::kError, "pid_error={1} process ended with ERROR: {2}")(Child_Name)(WEXITSTATUS(*status))); // Graceful exit, but there was an error
			return -1;
		}
	} else { // no PID returned
		if (errno == ECHILD)
			LOGERR("%s no child process exist\n", Child_Name.c_str());
		else {
			LOGERR("%s Unexpected error %d\n", Child_Name.c_str(), errno);
			return -1;
		}
	}
	return 0;
}

int TWFunc::Wait_For_Child_Timeout(pid_t pid, int *status,
				   const string & Child_Name, int timeout)
{
  pid_t retpid = waitpid(pid, status, WNOHANG);
  for (; retpid == 0 && timeout; --timeout)
    {
      sleep(1);
      retpid = waitpid(pid, status, WNOHANG);
    }
  if (retpid == 0 && timeout == 0)
    {
      LOGERR("%s took too long, killing process\n", Child_Name.c_str());
      kill(pid, SIGKILL);
      for (timeout = 5; retpid == 0 && timeout; --timeout)
	{
	  sleep(1);
	  retpid = waitpid(pid, status, WNOHANG);
	}
      if (retpid)
	LOGINFO("Child process killed successfully\n");
      else
	LOGINFO
	  ("Child process took too long to kill, may be a zombie process\n");
      return -1;
    }
  else if (retpid > 0)
    {
      if (WIFSIGNALED(*status))
	{
	  gui_msg(Msg(msg::kError, "pid_signal={1} process ended with signal: {2}") (Child_Name) (WTERMSIG(*status)));	// Seg fault or some other non-graceful termination
	  return -1;
	}
    }
  else if (retpid < 0)
    {				// no PID returned
      if (errno == ECHILD)
	LOGERR("%s no child process exist\n", Child_Name.c_str());
      else
	{
	  LOGERR("%s Unexpected error %d\n", Child_Name.c_str(), errno);
	  return -1;
	}
    }
  return 0;
}

bool TWFunc::Is_SymLink(string Path) {
  struct stat st;
  if ((lstat(Path.c_str(), &st) == 0) && (S_ISLNK(st.st_mode)))
     return true;
  else
     return false;
}

bool TWFunc::Path_Exists(string Path) {
	struct stat st;
	return stat(Path.c_str(), &st) == 0;
}

Archive_Type TWFunc::Get_File_Type(string fn)
{
  string::size_type i = 0;
  int firstbyte = 0, secondbyte = 0;
  char header[3];

  ifstream f;
  f.open(fn.c_str(), ios::in | ios::binary);
  f.get(header, 3);
  f.close();
  firstbyte = header[i] & 0xff;
  secondbyte = header[++i] & 0xff;

  if (firstbyte == 0x1f && secondbyte == 0x8b)
    return COMPRESSED;
  else if (firstbyte == 0x4f && secondbyte == 0x41)
    return ENCRYPTED;
  return UNCOMPRESSED;		// default
}

int TWFunc::Try_Decrypting_File(string fn, string password)
{
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
  OAES_CTX *ctx = NULL;
  uint8_t _key_data[32] = "";
  FILE *f;
  uint8_t buffer[4096];
  uint8_t *buffer_out = NULL;
  uint8_t *ptr = NULL;
  size_t read_len = 0, out_len = 0;
  int firstbyte = 0, secondbyte = 0;
  size_t _j = 0;
  size_t _key_data_len = 0;

  // mostly kanged from OpenAES oaes.c
  for (_j = 0; _j < 32; _j++)
    _key_data[_j] = _j + 1;
  _key_data_len = password.size();
  if (16 >= _key_data_len)
    _key_data_len = 16;
  else if (24 >= _key_data_len)
    _key_data_len = 24;
  else
    _key_data_len = 32;
  memcpy(_key_data, password.c_str(), password.size());

  ctx = oaes_alloc();
  if (ctx == NULL)
    {
      LOGERR("Failed to allocate OAES\n");
      return -1;
    }

  oaes_key_import_data(ctx, _key_data, _key_data_len);

  f = fopen(fn.c_str(), "rb");
  if (f == NULL)
    {
      LOGERR("Failed to open '%s' to try decrypt: %s\n", fn.c_str(),
	     strerror(errno));
      oaes_free(&ctx);
      return -1;
    }
  read_len = fread(buffer, sizeof(uint8_t), 4096, f);
  if (read_len <= 0)
    {
      LOGERR("Read size during try decrypt failed: %s\n", strerror(errno));
      fclose(f);
      oaes_free(&ctx);
      return -1;
    }
  if (oaes_decrypt(ctx, buffer, read_len, NULL, &out_len) != OAES_RET_SUCCESS)
    {
      LOGERR
	("Error: Failed to retrieve required buffer size for trying decryption.\n");
      fclose(f);
      oaes_free(&ctx);
      return -1;
    }
  buffer_out = (uint8_t *) calloc(out_len, sizeof(char));
  if (buffer_out == NULL)
    {
      LOGERR("Failed to allocate output buffer for try decrypt.\n");
      fclose(f);
      oaes_free(&ctx);
      return -1;
    }
  if (oaes_decrypt(ctx, buffer, read_len, buffer_out, &out_len) !=
      OAES_RET_SUCCESS)
    {
      LOGERR("Failed to decrypt file '%s'\n", fn.c_str());
      fclose(f);
      free(buffer_out);
      oaes_free(&ctx);
      return 0;
    }
  fclose(f);
  oaes_free(&ctx);
  if (out_len < 2)
    {
      LOGINFO("Successfully decrypted '%s' but read length too small.\n",
	      fn.c_str());
      free(buffer_out);
      return 1;			// Decrypted successfully
    }
  ptr = buffer_out;
  firstbyte = *ptr & 0xff;
  ptr++;
  secondbyte = *ptr & 0xff;
  if (firstbyte == 0x1f && secondbyte == 0x8b)
    {
      LOGINFO("Successfully decrypted '%s' and file is compressed.\n",
	      fn.c_str());
      free(buffer_out);
      return 3;			// Compressed
    }

  if (out_len >= 262)
    {
      ptr = buffer_out + 257;
      if (strncmp((char *) ptr, "ustar", 5) == 0)
	{
	  LOGINFO("Successfully decrypted '%s' and file is tar format.\n",
		  fn.c_str());
	  free(buffer_out);
	  return 2;		// Tar
	}
    }
  free(buffer_out);
  LOGINFO("No errors decrypting '%s' but no known file format.\n",
	  fn.c_str());
  return 1;			// Decrypted successfully
#else
  LOGERR("Encrypted backup support not included.\n");
  return -1;
#endif
}

unsigned long TWFunc::Get_File_Size(const string & Path)
{
  struct stat st;

  if (stat(Path.c_str(), &st) != 0)
    return 0;
  return st.st_size;
}

std::string TWFunc::Remove_Trailing_Slashes(const std::string & path,
					    bool leaveLast)
{
  std::string res;
  size_t last_idx = 0, idx = 0;

  while (last_idx != std::string::npos)
    {
      if (last_idx != 0)
	res += '/';

      idx = path.find_first_of('/', last_idx);
      if (idx == std::string::npos)
	{
	  res += path.substr(last_idx, idx);
	  break;
	}

      res += path.substr(last_idx, idx - last_idx);
      last_idx = path.find_first_not_of('/', idx);
    }

  if (leaveLast)
    res += '/';
  return res;
}

void TWFunc::Strip_Quotes(char *&str)
{
  if (strlen(str) > 0 && str[0] == '\"')
    str++;
  if (strlen(str) > 0 && str[strlen(str) - 1] == '\"')
    str[strlen(str) - 1] = 0;
}

vector < string > TWFunc::split_string(const string & in, char del,
				       bool skip_empty)
{
  vector < string > res;

  if (in.empty() || del == '\0')
    return res;

  string field;
  istringstream f(in);
  if (del == '\n')
    {
      while (getline(f, field))
	{
	  if (field.empty() && skip_empty)
	    continue;
	  res.push_back(field);
	}
    }
  else
    {
      while (getline(f, field, del))
	{
	  if (field.empty() && skip_empty)
	    continue;
	  res.push_back(field);
	}
    }
  return res;
}

timespec TWFunc::timespec_diff(timespec & start, timespec & end)
{
  timespec temp;
  if ((end.tv_nsec - start.tv_nsec) < 0)
    {
      temp.tv_sec = end.tv_sec - start.tv_sec - 1;
      temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    }
  else
    {
      temp.tv_sec = end.tv_sec - start.tv_sec;
      temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
  return temp;
}

int32_t TWFunc::timespec_diff_ms(timespec & start, timespec & end)
{
  return ((end.tv_sec * 1000) + end.tv_nsec / 1000000) -
    ((start.tv_sec * 1000) + start.tv_nsec / 1000000);
}

#ifndef BUILD_TWRPTAR_MAIN

// Returns "/path" from a full /path/to/file.name
string TWFunc::Get_Root_Path(const string & Path)
{
  string Local_Path = Path;

  // Make sure that we have a leading slash
  if (Local_Path.substr(0, 1) != "/")
    Local_Path = "/" + Local_Path;

  // Trim the path to get the root path only
  size_t position = Local_Path.find("/", 2);
  if (position != string::npos)
    {
      Local_Path.resize(position);
    }
  return Local_Path;
}

void TWFunc::install_htc_dumlock(void)
{
  int need_libs = 0;

	if (!PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), true))
		return;

  if (!PartitionManager.Mount_By_Path("/data", true))
    return;

  gui_msg("install_dumlock=Installing HTC Dumlock to system...");
  copy_file(TWHTCD_PATH "htcdumlocksys", "/system/bin/htcdumlock", 0755);
  if (!Path_Exists("/system/bin/flash_image"))
    {
      LOGINFO("Installing flash_image...\n");
      copy_file(TWHTCD_PATH "flash_imagesys", "/system/bin/flash_image",
		0755);
      need_libs = 1;
    }
  else
    LOGINFO("flash_image is already installed, skipping...\n");
  if (!Path_Exists("/system/bin/dump_image"))
    {
      LOGINFO("Installing dump_image...\n");
      copy_file(TWHTCD_PATH "dump_imagesys", "/system/bin/dump_image", 0755);
      need_libs = 1;
    }
  else
    LOGINFO("dump_image is already installed, skipping...\n");
  if (need_libs)
    {
      LOGINFO("Installing libs needed for flash_image and dump_image...\n");
      copy_file(TWHTCD_PATH "libbmlutils.so", "/system/lib/libbmlutils.so",
		0644);
      copy_file(TWHTCD_PATH "libflashutils.so",
		"/system/lib/libflashutils.so", 0644);
      copy_file(TWHTCD_PATH "libmmcutils.so", "/system/lib/libmmcutils.so",
		0644);
      copy_file(TWHTCD_PATH "libmtdutils.so", "/system/lib/libmtdutils.so",
		0644);
    }
  LOGINFO("Installing HTC Dumlock app...\n");
  mkdir("/data/app", 0777);
  unlink("/data/app/com.teamwin.htcdumlock*");
  copy_file(TWHTCD_PATH "HTCDumlock.apk",
	    "/data/app/com.teamwin.htcdumlock.apk", 0777);
  sync();
  gui_msg("done=Done.");
}


void TWFunc::htc_dumlock_restore_original_boot(void)
{
  if (!PartitionManager.Mount_By_Path("/sdcard", true))
    return;

  gui_msg("dumlock_restore=Restoring original boot...");
  Exec_Cmd("htcdumlock restore");
  gui_msg("done=Done.");
}


void TWFunc::htc_dumlock_reflash_recovery_to_boot(void)
{
  if (!PartitionManager.Mount_By_Path("/sdcard", true))
    return;
  gui_msg("dumlock_reflash=Reflashing recovery to boot...");
  Exec_Cmd("htcdumlock recovery noreboot");
  gui_msg("done=Done.");
}


int TWFunc::Recursive_Mkdir(string Path, bool ShowErr)
{
  std::vector < std::string > parts = Split_String(Path, "/", true);
  std::string cur_path;
  for (size_t i = 0; i < parts.size(); ++i)
    {
      cur_path += "/" + parts[i];
      if (!TWFunc::Path_Exists(cur_path))
	{
	  if (mkdir(cur_path.c_str(), 0777))
	    {
        if (ShowErr)
          gui_msg(Msg
            (msg::kError,
            "create_folder_strerr=Can not create '{1}' folder ({2}).")
            (cur_path) (strerror(errno)));
	      return false;
	    }
	  else
	    {
	      tw_set_default_metadata(cur_path.c_str());
	    }
	}
    }
  return true;
}


void TWFunc::GUI_Operation_Text(string Read_Value, string Default_Text)
{
  string Display_Text;

  DataManager::GetValue(Read_Value, Display_Text);
  if (Display_Text.empty())
    Display_Text = Default_Text;

  DataManager::SetValue("tw_operation", Display_Text);
  DataManager::SetValue("tw_partition", "");
}


void TWFunc::GUI_Operation_Text(string Read_Value, string Partition_Name,
				string Default_Text)
{
  string Display_Text;

  DataManager::GetValue(Read_Value, Display_Text);
  if (Display_Text.empty())
    Display_Text = Default_Text;

  DataManager::SetValue("tw_operation", Display_Text);
  DataManager::SetValue("tw_partition", Partition_Name);
}

void TWFunc::Copy_Log(string Source, string Destination) {
	int logPipe[2];
	int pigz_pid;
	int destination_fd;
	std::string destLogBuffer;

	PartitionManager.Mount_By_Path(Destination, false);

	size_t extPos = Destination.find(".gz");
	std::string uncompressedLog(Destination);
	uncompressedLog.replace(extPos, Destination.length(), "");

	if (Path_Exists(Destination)) {
		Archive_Type type = Get_File_Type(Destination);
		if (type == COMPRESSED) {
			std::string destFileBuffer;
			std::string getCompressedContents = "pigz -c -d " + Destination;
			if (Exec_Cmd(getCompressedContents, destFileBuffer) < 0) {
				LOGINFO("Unable to get destination logfile contents.\n");
				return;
			}
			destLogBuffer.append(destFileBuffer);
		}
	} else if (Path_Exists(uncompressedLog)) {
		std::ifstream uncompressedIfs(uncompressedLog);
		std::stringstream uncompressedSS;
		uncompressedSS << uncompressedIfs.rdbuf();
		uncompressedIfs.close();
		std::string uncompressedLogBuffer(uncompressedSS.str());
		destLogBuffer.append(uncompressedLogBuffer);
		std::remove(uncompressedLog.c_str());
	}

	std::ifstream ifs(Source);
	std::stringstream ss;
	ss << ifs.rdbuf();
	std::string srcLogBuffer(ss.str());
	ifs.close();

	if (pipe(logPipe) < 0) {
		LOGINFO("Unable to open pipe to write to persistent log file: %s\n", Destination.c_str());
	}

	destination_fd = open(Destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);

	pigz_pid = fork();
	if (pigz_pid < 0) {
		LOGINFO("fork() failed\n");
		close(destination_fd);
		close(logPipe[0]);
		close(logPipe[1]);
	} else if (pigz_pid == 0) {
		close(logPipe[1]);
		dup2(logPipe[0], fileno(stdin));
		dup2(destination_fd, fileno(stdout));
		if (execlp("pigz", "pigz", "-", NULL) < 0) {
			close(destination_fd);
			close(logPipe[0]);
			_exit(-1);
		}
	} else {
		close(logPipe[0]);
		if (write(logPipe[1], destLogBuffer.c_str(), destLogBuffer.size()) < 0) {
			LOGINFO("Unable to append to persistent log: %s\n", Destination.c_str());
			close(logPipe[1]);
			close(destination_fd);
			return;
		}
		if (write(logPipe[1], srcLogBuffer.c_str(), srcLogBuffer.size()) < 0) {
			LOGINFO("Unable to append to persistent log: %s\n", Destination.c_str());
			close(logPipe[1]);
			close(destination_fd);
			return;
		}
		close(logPipe[1]);
	}
	close(destination_fd);
}

void TWFunc::Update_Log_File(void) {
	std::string recoveryDir = get_log_dir() + "recovery/";

	if (get_log_dir() == CACHE_LOGS_DIR) {
		if (!PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false)) {
			LOGINFO("Failed to mount %s for TWFunc::Update_Log_File\n", CACHE_LOGS_DIR);
		}
	}

	if (!TWFunc::Path_Exists(recoveryDir)) {
		LOGINFO("Recreating %s folder.\n", recoveryDir.c_str());
		if (!Create_Dir_Recursive(recoveryDir,  S_IRWXU | S_IRWXG | S_IWGRP | S_IXGRP, 0, 0)) {
			LOGINFO("Unable to create %s folder.\n", recoveryDir.c_str());
		}
	}

	std::string logCopy = recoveryDir + "log.gz";
	std::string lastLogCopy = recoveryDir + "last_log.gz";
	copy_file(logCopy, lastLogCopy, 600);
	Copy_Log(TMP_LOG_FILE, logCopy);
	chown(logCopy.c_str(), 1000, 1000);
	chmod(logCopy.c_str(), 0600);
	chmod(lastLogCopy.c_str(), 0640);

	if (get_log_dir() == CACHE_LOGS_DIR) {
		if (PartitionManager.Mount_By_Path("/cache", false)) {
			if (unlink("/cache/recovery/command") && errno != ENOENT) {
				LOGINFO("Can't unlink %s\n", "/cache/recovery/command");
			}
		}
	}
	sync();
}

void TWFunc::Clear_Bootloader_Message() {
	std::string err;
	if (!clear_bootloader_message(&err)) {
	   LOGINFO("%s\n", err.c_str());
	}
}

void TWFunc::Update_Intent_File(string Intent)
{
  if (PartitionManager.Mount_By_Path("/cache", false) && !Intent.empty())
    {
      TWFunc::write_to_file("/cache/recovery/intent", Intent);
    }
}

// reboot: Reboot the system. Return -1 on error, no return on success
int TWFunc::tw_reboot(RebootCommand command)
{
	DataManager::Flush();
	Update_Log_File();

	// Always force a sync before we reboot
	sync();

  	// if we haven't called Deactivation_Process before, check whether to call it now 
  	// This code is currently disabled - Fox_AutoDeactivate_OnReboot is never set to 1
  	int DoDeactivate = 0;
  	if ((Fox_AutoDeactivate_OnReboot == 1) && (Fox_IsDeactivation_Process_Called == 0)) {
      	if ((DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1)       
	   || (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1)) { 
           	DoDeactivate = 1;
           }
    	}
    	
  	TWFunc::Run_Before_Reboot();
  	// ----
   
	switch (command) {
		case rb_current:
		case rb_system:
      			if (DoDeactivate == 1) { Deactivation_Process(); } 
			Update_Intent_File("s");
			sync();
			check_and_run_script("/system/bin/rebootsystem.sh", "reboot system");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "reboot,");
#elif defined(ANDROID_RB_RESTART)
			return android_reboot(ANDROID_RB_RESTART, 0, 0);
#else
			return reboot(RB_AUTOBOOT);
#endif
		case rb_recovery:
      			if (DoDeactivate == 1) { Deactivation_Process(); } 
			check_and_run_script("/system/bin/rebootrecovery.sh", "reboot recovery");
			return property_set(ANDROID_RB_PROPERTY, "reboot,recovery");
		case rb_bootloader:
			check_and_run_script("/system/bin/rebootbootloader.sh", "reboot bootloader");
			return property_set(ANDROID_RB_PROPERTY, "reboot,bootloader");
		case rb_poweroff:
			check_and_run_script("/system/bin/poweroff.sh", "power off");
#ifdef ANDROID_RB_PROPERTY
			return property_set(ANDROID_RB_PROPERTY, "shutdown,");
#elif defined(ANDROID_RB_POWEROFF)
			return android_reboot(ANDROID_RB_POWEROFF, 0, 0);
#else
			return reboot(RB_POWER_OFF);
#endif
		case rb_download:
			check_and_run_script("/system/bin/rebootdownload.sh", "reboot download");
			return property_set(ANDROID_RB_PROPERTY, "reboot,download");
		case rb_edl:
			check_and_run_script("/system/bin/rebootedl.sh", "reboot edl");
			return property_set(ANDROID_RB_PROPERTY, "reboot,edl");
		case rb_fastboot:
			return property_set(ANDROID_RB_PROPERTY, "reboot,fastboot");
		default:
			return -1;
	}
	return -1;
}


void TWFunc::check_and_run_script(const char *script_file,
				  const char *display_name)
{
  // Check for and run startup script if script exists
  struct stat st;
  if (stat(script_file, &st) == 0)
    {
      gui_msg(Msg("run_script=Running {1} script...")(display_name));
      chmod(script_file, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
      TWFunc::Exec_Cmd(script_file);
      gui_msg("done=Done.");
    }
}

int TWFunc::removeDir(const string path, bool skipParent)
{
  DIR *d = opendir(path.c_str());
  int r = 0;
  string new_path;

  if (d == NULL)
    {
      gui_msg(Msg
	      (msg::kError,
	       "error_opening_strerr=Error opening: '{1}' ({2})") (path)
	      (strerror(errno)));
      return -1;
    }

  if (d)
    {
      struct dirent *p;
      while (!r && (p = readdir(d)))
	{
	  if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
	    continue;
	  new_path = path + "/";
	  new_path.append(p->d_name);
	  if (p->d_type == DT_DIR)
	    {
	      r = removeDir(new_path, true);
	      if (!r)
		{
		  if (p->d_type == DT_DIR)
		    r = rmdir(new_path.c_str());
		  else
		    LOGINFO("Unable to removeDir '%s': %s\n",
			    new_path.c_str(), strerror(errno));
		}
	    }
	  else if (p->d_type == DT_REG || p->d_type == DT_LNK
		   || p->d_type == DT_FIFO || p->d_type == DT_SOCK)
	    {
	      r = unlink(new_path.c_str());
	      if (r != 0)
		{
		  LOGINFO("Unable to unlink '%s: %s'\n", new_path.c_str(),
			  strerror(errno));
		}
	    }
	}
      closedir(d);

      if (!r)
	{
	  if (skipParent)
	    return 0;
	  else
	    r = rmdir(path.c_str());
	}
    }
  return r;
}

int TWFunc::copy_file(string src, string dst, int mode) {
	PartitionManager.Mount_By_Path(src, false);
	PartitionManager.Mount_By_Path(dst, false);

	if (!Path_Exists(src)) {
		LOGINFO("Unable to find source file %s\n", src.c_str());
		return -1;
	}
	std::ifstream srcfile(src, ios::binary);
	std::ofstream dstfile(dst, ios::binary);
	dstfile << srcfile.rdbuf();
	if (!dstfile.bad()) {
		LOGINFO("Copied file %s to %s\n", src.c_str(), dst.c_str());
	}
	else {
		LOGINFO("Unable to copy file %s to %s\n", src.c_str(), dst.c_str());
		return -1;
	}

	srcfile.close();
	dstfile.close();
	if (chmod(dst.c_str(), mode) != 0)
		return -1;
	return 0;
}

unsigned int TWFunc::Get_D_Type_From_Stat(string Path)
{
  struct stat st;

  stat(Path.c_str(), &st);
  if (st.st_mode & S_IFDIR)
    return DT_DIR;
  else if (st.st_mode & S_IFBLK)
    return DT_BLK;
  else if (st.st_mode & S_IFCHR)
    return DT_CHR;
  else if (st.st_mode & S_IFIFO)
    return DT_FIFO;
  else if (st.st_mode & S_IFLNK)
    return DT_LNK;
  else if (st.st_mode & S_IFREG)
    return DT_REG;
  else if (st.st_mode & S_IFSOCK)
    return DT_SOCK;
  return DT_UNKNOWN;
}

int TWFunc::read_file(string fn, string & results)
{
  ifstream file;
  file.open(fn.c_str(), ios::in);

  if (file.is_open())
    {
      file >> results;
      file.close();
      return 0;
    }

  LOGINFO("Cannot find file %s\n", fn.c_str());
  return -1;
}

int TWFunc::read_file(string fn, vector < string > &results)
{
  ifstream file;
  string line;
  file.open(fn.c_str(), ios::in);
  if (file.is_open())
    {
      while (getline(file, line))
	results.push_back(line);
      file.close();
      return 0;
    }
  LOGINFO("Cannot find file %s\n", fn.c_str());
  return -1;
}

int TWFunc::read_file(string fn, vector < wstring > &results)
{
  wifstream file;
  wstring line;
  file.open(fn.c_str(), ios::in);
  if (file.is_open())
    {
      while (getline(file, line))
	results.push_back(line);
      file.close();
      return 0;
    }
  LOGINFO("Cannot find file %s\n", fn.c_str());
  return -1;
}

string TWFunc::wstr_to_str(wstring wstr) {
  using convert_type = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_type, wchar_t> converter;

  return converter.to_bytes(wstr);
}

int TWFunc::read_file(string fn, uint64_t & results)
{
  ifstream file;
  file.open(fn.c_str(), ios::in);

  if (file.is_open())
    {
      file >> results;
      file.close();
      return 0;
    }

  LOGINFO("Cannot find file %s\n", fn.c_str());
  return -1;
}

int TWFunc::write_to_file(const string & fn, const string & line)
{
  FILE *file;
  file = fopen(fn.c_str(), "w");
  if (file != NULL)
    {
      fwrite(line.c_str(), line.size(), 1, file);
      fclose(file);
      return 0;
    }
  LOGINFO("Cannot find file %s\n", fn.c_str());
  return -1;
}

bool TWFunc::Try_Decrypting_Backup(string Restore_Path, string Password) {
	DIR* d;

  string Filename;
  Restore_Path += "/";
  d = opendir(Restore_Path.c_str());
  if (d == NULL)
    {
      gui_msg(Msg
	      (msg::kError,
	       "error_opening_strerr=Error opening: '{1}' ({2})")
	      (Restore_Path) (strerror(errno)));
      return false;
    }

  struct dirent *de;
  while ((de = readdir(d)) != NULL)
    {
      Filename = Restore_Path;
      Filename += de->d_name;
      if (TWFunc::Get_File_Type(Filename) == ENCRYPTED)
	{
	  if (TWFunc::Try_Decrypting_File(Filename, Password) < 2)
	    {
	      DataManager::SetValue("tw_restore_password", "");	// Clear the bad password
	      DataManager::SetValue("tw_restore_display", "");	// Also clear the display mask
	      closedir(d);
	      return false;
	    }
	}
    }
  closedir(d);
  return true;
}

string TWFunc::Get_Current_Date()
{
  string Current_Date;
  time_t seconds = time(0);
  struct tm *t = localtime(&seconds);
  char timestamp[255];
  sprintf(timestamp, "%04d-%02d-%02d--%02d-%02d-%02d", t->tm_year + 1900,
	  t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  Current_Date = timestamp;
  return Current_Date;
}

string TWFunc::System_Property_Get(string Prop_Name) {
	return System_Property_Get(Prop_Name, PartitionManager, PartitionManager.Get_Android_Root_Path());
}

string TWFunc::System_Property_Get(string Prop_Name, TWPartitionManager &PartitionManager, string Mount_Point) {
	bool mount_state = PartitionManager.Is_Mounted_By_Path(Mount_Point);
	std::vector<string> buildprop;
	string propvalue;
	if (!PartitionManager.Mount_By_Path(Mount_Point, true) && !mount_state)
		return propvalue;
	string prop_file = Mount_Point + "/build.prop";
	if (!TWFunc::Path_Exists(prop_file))
		prop_file = Mount_Point + "/system/build.prop"; // for devices with system as a root file system (e.g. Pixel)
	if (TWFunc::read_file(prop_file, buildprop) != 0) {
		LOGINFO("Unable to open build.prop for getting '%s'.\n", Prop_Name.c_str());
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		if (!mount_state)
			PartitionManager.UnMount_By_Path(Mount_Point, false);
		return propvalue;
	}
	int line_count = buildprop.size();
	int index;
	size_t start_pos = 0, end_pos;
	string propname;
	for (index = 0; index < line_count; index++) {
		end_pos = buildprop.at(index).find("=", start_pos);
		propname = buildprop.at(index).substr(start_pos, end_pos);
		if (propname == Prop_Name) {
			propvalue = buildprop.at(index).substr(end_pos + 1, buildprop.at(index).size());
			if (!mount_state)
				PartitionManager.UnMount_By_Path(Mount_Point, false);
			return propvalue;
		}
	}
	if (!mount_state)
		PartitionManager.UnMount_By_Path(Mount_Point, false);
	return propvalue;
}

string TWFunc::File_Property_Get(string File_Path, string Prop_Name)
{
  std::vector < string > buildprop;
  string propvalue;
  string prop_file = File_Path;
  if (TWFunc::read_file(prop_file, buildprop) != 0)
    {
      return propvalue;
    }
  int line_count = buildprop.size();
  int index;
  size_t start_pos = 0, end_pos;
  string propname;
  for (index = 0; index < line_count; index++)
    {
      end_pos = buildprop.at(index).find("=", start_pos);
      propname = buildprop.at(index).substr(start_pos, end_pos);
      if (propname == Prop_Name)
	{
	  propvalue =
	    buildprop.at(index).substr(end_pos + 1,
				       buildprop.at(index).size());
	  return propvalue;
	}
    }
  return propvalue;
}

void TWFunc::Auto_Generate_Backup_Name() {
	string propvalue = System_Property_Get("ro.build.display.id");
	if (propvalue.empty()) {
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
		return;
	}
	else {
		//remove periods from build display so it doesn't confuse the extension code
		propvalue.erase(remove(propvalue.begin(), propvalue.end(), '.'), propvalue.end());
	}
	string Backup_Name = Get_Current_Date();
	Backup_Name += "_" + propvalue;
	if (Backup_Name.size() > MAX_BACKUP_NAME_LEN)
		Backup_Name.resize(MAX_BACKUP_NAME_LEN);
	// Trailing spaces cause problems on some file systems, so remove them
	string space_check, space = " ";
	space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
	while (space_check == space) {
		Backup_Name.resize(Backup_Name.size() - 1);
		space_check = Backup_Name.substr(Backup_Name.size() - 1, 1);
	}
	replace(Backup_Name.begin(), Backup_Name.end(), ' ', '_');
	if (PartitionManager.Check_Backup_Name(Backup_Name, false, true) != 0) {
		LOGINFO("Auto generated backup name '%s' is not valid, using date instead.\n", Backup_Name.c_str());
		DataManager::SetValue(TW_BACKUP_NAME, Get_Current_Date());
	} else {
		DataManager::SetValue(TW_BACKUP_NAME, Backup_Name);
	}
}

int TWFunc::Property_Override(string Prop_Name, string Prop_Value) {
#ifdef TW_INCLUDE_LIBRESETPROP
    return setprop(Prop_Name.c_str(), Prop_Value.c_str(), false);
#else
    if (TWFunc::Fox_Property_Set(Prop_Name, Prop_Value))
    	return 0;
    else
    	return -1;
#endif
}

void TWFunc::Fixup_Time_On_Boot(const string & time_paths)
{
#ifdef QCOM_RTC_FIX
  static bool fixed = false;
  
  if (fixed) // i.e., we have sorted out the date/time issue properly
   {
    return;
   }

  LOGINFO("TWFunc::Fixup_Time: Pre-fix date and time: %s\n", TWFunc::Get_Current_Date().c_str());
  
  struct timeval tv;
  uint64_t offset = 0;
  uint64_t drift = 0;
  int store = 0;
  unsigned long long stored_drift = 0;
  const uint64_t min_offset = 1585559739; // minimum offset = Mon Mar 30 10:15:39 BST 2020
  std::string sepoch = "/sys/class/rtc/rtc0/since_epoch";

  if (TWFunc::read_file(sepoch, offset) == 0)
    {

      LOGINFO("TWFunc::Fixup_Time: Setting time offset from file %s\n", sepoch.c_str());

      // DJ9
      if (offset < 1261440000) // bad RTC (less than 41 years since epoch!)
      {  
	 // try to correct 	 
	 if (DataManager::GetValue("fox_epoch_drift", stored_drift) < 0) // read from .foxs
	 	stored_drift = 0;
	 
	 #ifndef OF_DEVICE_WITHOUT_PERSIST
	 if (TWFunc::read_file (epoch_drift_file, drift) != 0) // read from epoch drift file
	 	drift = 0;
	 #endif
	 
	 // what have we succeeded in reading?
         if ((drift > 0) || (stored_drift > 0)) 
         {
            if ((stored_drift > 0) && (drift == 0)) // we only got drift from .foxs
                {
           	  drift = stored_drift;
           	  LOGINFO ("TWFunc::Fixup_Time: Using drift value (%llu) stored in .foxs\n", (unsigned long long)drift);
                } 
            else 
            {   
           	 if ((stored_drift > 0) && (drift > 0)) // we got two values
              	 {
                    if (stored_drift != drift) // let drift override stored_drift, and save drift
                    {
                        store = 1; 
                    } 
                    LOGINFO ("TWFunc::Fixup_Time: using drift value (%llu) stored in %s\n", (unsigned long long)drift, epoch_drift_file.c_str());
              	 } 
              	 else // either both are 0, or stored_drift is 0
              	 {
              	    if (drift > 0) // then stored_drift must be 0
              	    {
                        LOGINFO ("TWFunc::Fixup_Time: using drift value (%llu) stored in %s\n", (unsigned long long)drift, epoch_drift_file.c_str());
              	    	store = 1;
              	    }
              	 }             
             }
             
	   // now see if we have a sensible value            
           if (drift > 1369180500) // ignore drifts from earlier than Tuesday, May 21, 2013 11:55:00 PM
           { 
              offset += drift; 
              LOGINFO ("TWFunc::Fixup_Time: Compensated for drift by %llu \n", (unsigned long long)drift);
              DataManager::SetValue("tw_qcom_ats_offset", (unsigned long long) offset, 1); // store this new offset
              
              if (store == 1) // we haven't already stored the drift in .foxs
              	DataManager::SetValue("fox_epoch_drift", (unsigned long long) drift, 1); // store the drift value
           }
           
         } // if (drift > 0) || (stored_drift > 0)
      }	// if offset
      // DJ9
      
      tv.tv_sec = offset;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      gettimeofday(&tv, NULL);
      
      offset = tv.tv_sec;  // this and the next line to get rid of warnings
      if (offset > min_offset)
	{
	  LOGINFO("TWFunc::Fixup_Time: Date and time corrected: %s\n", TWFunc::Get_Current_Date().c_str());
	  fixed = true;
	  return;
	} 
	else 
	{
      	  LOGINFO("TWFunc::Fixup_Time: Wrong date and time epoch in %s\n", sepoch.c_str());
	}
    }
  else
    {
      LOGINFO("TWFunc::Fixup_Time: opening %s failed\n", sepoch.c_str());
    }

  LOGINFO("TWFunc::Fixup_Time: will attempt to use the ats files now.\n");

  // Devices with Qualcomm Snapdragon 800 do some shenanigans with RTC.
  // They never set it, it just ticks forward from 1970-01-01 00:00,
  // and then they have files /data/system/time/ats_* with 64bit offset
  // in miliseconds which, when added to the RTC, gives the correct time.
  // So, the time is: (offset_from_ats + value_from_RTC)
  // There are multiple ats files, they are for different systems? Bases?
  // Like, ats_1 is for modem and ats_2 is for TOD (time of day?).
  // Look at file time_genoff.h in CodeAurora, qcom-opensource/time-services

  std::vector < std::string > paths;	// space separated list of paths
  if (time_paths.empty())
    {
      paths = Split_String("/data/system/time/ /data/time/ /data/vendor/time/", " ");
      if (!PartitionManager.Mount_By_Path("/data", false))
	return;
    }
  else
    {
      // When specific path(s) are used, Fixup_Time needs those
      // partitions to already be mounted!
      paths = Split_String(time_paths, " ");
    }

  FILE *f;
  offset = 0;
  struct dirent *dt;
  std::string ats_path;

  // Prefer ats_2, it seems to be the one we want according to logcat on hammerhead
  // - it is the one for ATS_TOD (time of day?).
  // However, I never saw a device where the offset differs between ats files.
  for (size_t i = 0; i < paths.size(); ++i)
    {
      DIR *d = opendir(paths[i].c_str());
      if (!d)
	continue;

      while ((dt = readdir(d)))
	{
	  if (dt->d_type != DT_REG || strncmp(dt->d_name, "ats_", 4) != 0)
	    continue;

	  if (ats_path.empty() || strcmp(dt->d_name, "ats_2") == 0)
	    ats_path = paths[i] + dt->d_name;
	}

      closedir(d);
    }

  if (ats_path.empty())
    {
      LOGINFO("TWFunc::Fixup_Time: no ats files found, leaving untouched!\n");
    }
  else if ((f = fopen(ats_path.c_str(), "r")) == NULL)
    {
      LOGINFO("TWFunc::Fixup_Time: failed to open file %s\n",
	      ats_path.c_str());
    }
  else if (fread(&offset, sizeof(offset), 1, f) != 1)
    {
      LOGINFO("TWFunc::Fixup_Time: failed load uint64 from file %s\n",
	      ats_path.c_str());
      fclose(f);
    }
  else
    {
      fclose(f);

      LOGINFO
	("TWFunc::Fixup_Time: Setting time offset from file %s, offset %llu\n",
	 ats_path.c_str(), (unsigned long long) offset);
      DataManager::SetValue("tw_qcom_ats_offset", (unsigned long long) offset, 1);
      fixed = true;
    }

  if (!fixed)
    {
      // Failed to get offset from ats file, check twrp settings
      unsigned long long value;
      if (DataManager::GetValue("tw_qcom_ats_offset", value) < 0)
	{
	  return;
	}
      else
	{
	  // Do not consider the settings file as a definitive answer, keep fixed=false so next run will try ats files again
	  offset = (uint64_t) value;
	  LOGINFO
	    ("TWFunc::Fixup_Time: Setting time offset from twrp setting file, offset %llu\n",
	     (unsigned long long) offset);
	        tv.tv_sec = offset;
      		tv.tv_usec = 0;
      		settimeofday(&tv, NULL);
      		gettimeofday(&tv, NULL);
      		return;
	}
    }

  gettimeofday(&tv, NULL);

  tv.tv_sec += offset / 1000;
  
#ifdef TW_CLOCK_OFFSET
// Some devices are even quirkier and have ats files that are offset from the actual time
	tv.tv_sec = tv.tv_sec + TW_CLOCK_OFFSET;
#endif
  
  tv.tv_usec += (offset % 1000) * 1000;

  while (tv.tv_usec >= 1000000)
    {
      ++tv.tv_sec;
      tv.tv_usec -= 1000000;
    }

  settimeofday(&tv, NULL);

// last check for sensible offset
    if (offset < min_offset) // let's try something else 
    {
      LOGINFO("TWFunc::Fixup_Time: trying for the last time!\n");
      tv.tv_sec = min_offset;
      tv.tv_usec = 0;   
      settimeofday(&tv, NULL);
      gettimeofday(&tv, NULL);
    }
//

  LOGINFO("TWFunc::Fixup_Time: Date and time corrected: %s\n", TWFunc::Get_Current_Date().c_str());
#endif
}

std::vector < std::string > TWFunc::Split_String(const std::string & str,
						 const std::
						 string & delimiter,
						 bool removeEmpty)
{
  std::vector < std::string > res;
  size_t idx = 0, idx_last = 0;

  while (idx < str.size())
    {
      idx = str.find_first_of(delimiter, idx_last);
      if (idx == std::string::npos)
	idx = str.size();

      if (idx - idx_last != 0 || !removeEmpty)
	res.push_back(str.substr(idx_last, idx - idx_last));

      idx_last = idx + delimiter.size();
    }

  return res;
}

bool TWFunc::Create_Dir_Recursive(const std::string & path, mode_t mode,
				  uid_t uid, gid_t gid)
{
  std::vector < std::string > parts = Split_String(path, "/");
  std::string cur_path;
  struct stat info;
  for (size_t i = 0; i < parts.size(); ++i)
    {
      cur_path += "/" + parts[i];
      if (stat(cur_path.c_str(), &info) < 0 || !S_ISDIR(info.st_mode))
	{
	  if (mkdir(cur_path.c_str(), mode) < 0)
	    return false;
	  chown(cur_path.c_str(), uid, gid);
	}
    }
  return true;
}

int TWFunc::Set_Brightness(std::string brightness_value)
{
  int result = -1;
  std::string secondary_brightness_file;

  if (DataManager::GetIntValue("tw_has_brightnesss_file"))
    {
      LOGINFO("TWFunc::Set_Brightness: Setting brightness control to %s\n",
	      brightness_value.c_str());
      result =
	TWFunc::write_to_file(DataManager::GetStrValue("tw_brightness_file"),
			      brightness_value);
      DataManager::GetValue("tw_secondary_brightness_file",
			    secondary_brightness_file);
      if (!secondary_brightness_file.empty())
	{
	  LOGINFO
	    ("TWFunc::Set_Brightness: Setting secondary brightness control to %s\n",
	     brightness_value.c_str());
	  TWFunc::write_to_file(secondary_brightness_file, brightness_value);
	}
    }
  return result;
}

bool TWFunc::Toggle_MTP(bool enable)
{
#ifdef TW_HAS_MTP
  static int was_enabled = false;

  if (enable && was_enabled)
    {
      if (!PartitionManager.Enable_MTP())
	PartitionManager.Disable_MTP();
    }
  else
    {
      was_enabled = DataManager::GetIntValue("tw_mtp_enabled");
      PartitionManager.Disable_MTP();
      usleep(500);
    }
  return was_enabled;
#else
  return false;
#endif
}

void TWFunc::SetPerformanceMode(bool mode)
{
  if (mode)
    {
      property_set("recovery.perf.mode", "1");
    }
  else
    {
      property_set("recovery.perf.mode", "0");
    }
  // Some time for events to catch up to init handlers
  usleep(500000);
}

std::string TWFunc::to_string(unsigned long value)
{
  std::ostringstream os;
  os << value;
  return os.str();
}

void TWFunc::Disable_Stock_Recovery_Replace_Func(void)
{
     if (DataManager::GetIntValue(FOX_DONT_REPLACE_STOCK) == 1)
      	return;

     usleep(128);
     if ((DataManager::GetIntValue(FOX_ADVANCED_STOCK_REPLACE) == 1) 
      ||  (Fox_Force_Deactivate_Process == 1))
	{
      	  bool we_mounted = false;
      	  bool we_mounted_sys = false;
      	  string thedir = "/system";
          string rootdir = PartitionManager.Get_Android_Root_Path();

      	// system-as-root stuff
      	  bool Is_SysRoot = Has_System_Root();
      	  if (Is_SysRoot)
            {
	       if (TWFunc::Path_Exists(rootdir + "/system") && TWFunc::Path_Exists(rootdir + "/system/etc"))
	         {
                     rootdir = rootdir + "/system";
                 }
               else
              	 {
            	   if (!PartitionManager.Is_Mounted_By_Path("/system"))
             	      {
                	if (PartitionManager.Mount_By_Path("/system", false))
                	   {
                              we_mounted_sys = true;
                   	   }
             	      }
            	   if ((PartitionManager.Is_Mounted_By_Path("/system")) && (TWFunc::Path_Exists("/system/system")))
            	     {
                	rootdir = "/system/system";
                     }
              	 }
            }
          else // it is not system-as-root
            {
              if (rootdir != "/system")
                {
	           if (rootdir != "/")
	              rootdir = rootdir + "/";
	       
	           if (!PartitionManager.Is_Mounted_By_Path(rootdir + "system"))
	              {
	           	if (PartitionManager.Mount_By_Path(rootdir + "system", false))
	              	   {
	                	we_mounted_sys = true;
	                	thedir = rootdir + "system";
	                   }
	              }

	       	   if (TWFunc::Path_Exists(rootdir + "system") && TWFunc::Path_Exists(rootdir + "system/etc"))
	              {
                          rootdir = rootdir + "system";
                      }
                }
            }
     	// system-as-root stuff //

          LOGINFO("OrangeFox: Disabling stock recovery [search-dir=%s]...\n", rootdir.c_str());

	// using rootdir/ as determined here
	  usleep(512);
	  if (TWFunc::Path_Exists(rootdir))
	    {
          	LOGINFO("OrangeFox: checking %s ...\n", rootdir.c_str());
	  	if (Path_Exists(rootdir + "/bin/install-recovery.sh"))
	      		Rename_File(rootdir + "/bin/install-recovery.sh",
		     	  	rootdir + "/bin/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists(rootdir + "/etc/install-recovery.sh"))
	      		Rename_File(rootdir + "/etc/install-recovery.sh",
		   	 	rootdir + "/etc/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists(rootdir + "/etc/recovery-resource.dat"))
	      		Rename_File(rootdir + "/etc/recovery-resource.dat",
		   	  	rootdir + "/etc/wlfx0recovery-resource0xwlf");

          	if (Path_Exists(rootdir + "/recovery-from-boot.p"))
  	     	   {
	         	Rename_File(rootdir + "/recovery-from-boot.p",
		      	     rootdir + "/wlfx0recovery-from-boot.bak0xwlf");
	          	sync();
	     	   }
	     }

	// using hardcoded /system/vendor/
	  we_mounted = false;
	  
	  if (!Is_SymLink("/system") && !PartitionManager.Is_Mounted_By_Path("/system"))
             {
               if (PartitionManager.Mount_By_Path("/system", false))
                 {
                    we_mounted = true;
                 }
             }

	  if (!Is_SymLink("/system") && PartitionManager.Is_Mounted_By_Path("/system"))
	     {
          	LOGINFO("OrangeFox: checking /system ...\n");
	  	if (Path_Exists("/system/vendor/bin/install-recovery.sh"))
	    		rename("/system/vendor/bin/install-recovery.sh",
		   		"/system/vendor/bin/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists("/system/vendor/etc/install-recovery.sh"))
	    		rename("/system/vendor/etc/install-recovery.sh",
		   		"/system/vendor/etc/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists("/system/vendor/etc/recovery-resource.dat"))
	    		rename("/system/vendor/etc/recovery-resource.dat",
		   		"/system/vendor/etc/wlfx0recovery-resource0xwlf");

          	if (Path_Exists("/system/vendor/recovery-from-boot.p"))
  	     	   {
	         	Rename_File("/system/vendor/recovery-from-boot.p",
		      	     "/system/vendor/wlfx0recovery-from-boot.bak0xwlf");
	     	   }

	  	usleep(512);	  	
	  	if (we_mounted) // cleanup
	     	    PartitionManager.UnMount_By_Path("/system", false);
	     }

	  usleep(512);
	  if (we_mounted_sys) // cleanup
	    {
	       if (PartitionManager.Is_Mounted_By_Path(thedir))
	          PartitionManager.UnMount_By_Path(thedir, false);
	    }

	// using hardcoded /vendor/
	  usleep(512);
	  we_mounted = false;
	  if (!PartitionManager.Is_Mounted_By_Path("/vendor"))
             {
               if (PartitionManager.Mount_By_Path("/vendor", false))
                 {
                    we_mounted = true;
                 }
             }

	  if (PartitionManager.Is_Mounted_By_Path("/vendor"))
	     {
          	LOGINFO("OrangeFox: checking /vendor ...\n");
	  	if (Path_Exists("/vendor/bin/install-recovery.sh"))
	    		rename("/vendor/bin/install-recovery.sh",
		   	"/vendor/bin/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists("/vendor/etc/install-recovery.sh"))
	    		rename("/vendor/etc/install-recovery.sh",
		   	"/vendor/etc/wlfx0install-recoverybak0xwlf");

	  	if (Path_Exists("/vendor/etc/recovery-resource.dat"))
	    		rename("/vendor/etc/recovery-resource.dat",
		   	"/vendor/etc/wlfx0recovery-resource0xwlf");

          	if (Path_Exists("/vendor/recovery-from-boot.p"))
  	     	   {
	         	Rename_File("/vendor/recovery-from-boot.p",
		      	     "/vendor/wlfx0recovery-from-boot.bak0xwlf");
	     	   }
	  	
	  	usleep(512);
	  	if (we_mounted) // cleanup
	     	    PartitionManager.UnMount_By_Path("/vendor", false);
	     }
      usleep(64);
      sync();
      }
}

// Disable flashing of stock recovery
void TWFunc::Disable_Stock_Recovery_Replace(void)
{
  #ifdef OF_VANILLA_BUILD
  return;
  #endif
  if (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false))
     { 
         Disable_Stock_Recovery_Replace_Func();           
         PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
     }
}

unsigned long long TWFunc::IOCTL_Get_Block_Size(const char *block_device)
{
  unsigned long block_device_size;
  int ret = 0;

  int fd = open(block_device, O_RDONLY);
  if (fd < 0)
    {
      LOGINFO("Find_Partition_Size: Failed to open '%s', (%s)\n",
	      block_device, strerror(errno));
    }
  else
    {
      ret = ioctl(fd, BLKGETSIZE, &block_device_size);
      close(fd);
      if (ret)
	{
	  LOGINFO("Find_Partition_Size: ioctl error: (%s)\n",
		  strerror(errno));
	}
      else
	{
	  return (unsigned long long) (block_device_size) * 512LLU;
	}
    }
  return 0;
}


bool TWFunc::CheckWord(std::string filename, std::string search)
{
  std::string line;
  ifstream File;
  File.open(filename);
  if (File.is_open())
    {
      while (!File.eof())
	{
	  std::getline(File, line);
	  if (line.find(search) != string::npos)
	    {
	      File.close();
	      return true;
	    }
	}
      File.close();
    }
  return false;
}

void TWFunc::Replace_Word_In_File(string file_path, string search,
				  string word)
{
  std::string contents_of_file, local, renamed = file_path + ".wlfx";
  if (TWFunc::Path_Exists(renamed))
    unlink(renamed.c_str());
  std::rename(file_path.c_str(), renamed.c_str());
  std::ifstream old_file(renamed.c_str());
  std::ofstream new_file(file_path.c_str());
  size_t start_pos, end_pos, pos;
  while (std::getline(old_file, contents_of_file))
    {
      start_pos = 0;
      pos = 0;
      end_pos = search.find(";", start_pos);
      while (end_pos != string::npos && start_pos < search.size())
	{
	  local = search.substr(start_pos, end_pos - start_pos);
	  if (contents_of_file.find(local) != string::npos)
	    {
	      while ((pos =
		      contents_of_file.find(local, pos)) != string::npos)
		{
		  contents_of_file.replace(pos, local.length(), word);
		  pos += word.length();
		}
	    }
	  start_pos = end_pos + 1;
	  end_pos = search.find(";", start_pos);
	}
      new_file << contents_of_file << '\n';
    }
  unlink(renamed.c_str());
  chmod(file_path.c_str(), 0644);
}

void TWFunc::Replace_Word_In_File(std::string file_path, std::string search)
{
  std::string contents_of_file, local, renamed = file_path + ".wlfx";
  if (TWFunc::Path_Exists(renamed))
    unlink(renamed.c_str());
  std::rename(file_path.c_str(), renamed.c_str());
  std::ifstream old_file(renamed.c_str());
  std::ofstream new_file(file_path.c_str());
  size_t start_pos, end_pos, pos;
  while (std::getline(old_file, contents_of_file))
    {
      start_pos = 0;
      pos = 0;
      end_pos = search.find(";", start_pos);
      while (end_pos != string::npos && start_pos < search.size())
	{
	  local = search.substr(start_pos, end_pos - start_pos);
	  if (contents_of_file.find(local) != string::npos)
	    {
	      while ((pos =
		      contents_of_file.find(local, pos)) != string::npos)
		contents_of_file.replace(pos, local.length(), "");
	    }
	  start_pos = end_pos + 1;
	  end_pos = search.find(";", start_pos);
	}
      new_file << contents_of_file << '\n';
    }
  unlink(renamed.c_str());
  chmod(file_path.c_str(), 0644);
}

void TWFunc::Remove_Word_From_File(std::string file_path, std::string search)
{
   Replace_Word_In_File(file_path, search);
}

void TWFunc::Set_New_Ramdisk_Property(std::string file_path, std::string prop,
				      bool enable)
{
  if (TWFunc::CheckWord(file_path, prop))
    {
      if (enable)
	{
	  std::string expected_value = prop + "=0";
	  prop += "=1";
	  TWFunc::Replace_Word_In_File(file_path, expected_value, prop);
	}
      else
	{
	  std::string expected_value = prop + "=1";
	  prop += "=0";
	  TWFunc::Replace_Word_In_File(file_path, expected_value, prop);
	}
    }
  else
    {
      ofstream File(file_path.c_str(), std::ios::app);
      if (File.is_open())
	{
	  if (enable)
	    prop += "=1";
	  else
	    prop += "=0";
	  File << prop;
	  File.close();
	}
    }
}


void TWFunc::Write_MIUI_Install_Status(std::string install_status,
				       bool verify)
{
  std::string last_status = "/cache/recovery/last_status";
  if (!verify)
    {
      if (DataManager::GetIntValue(FOX_MIUI_ZIP_TMP) != 0
	  || DataManager::GetIntValue(FOX_METADATA_PRE_BUILD) != 0)
	{
	  if (PartitionManager.Mount_By_Path("/cache", true))
	    {
	      if (Path_Exists(last_status))
		unlink(last_status.c_str());

	      ofstream status;
	      status.open(last_status.c_str());
	      status << install_status;
	      status.close();
	    }
	}
    }
  else if (PartitionManager.Mount_By_Path("/cache", true)
	   && DataManager::GetIntValue(FOX_INCREMENTAL_PACKAGE) != 0)
    {
      if (Path_Exists(last_status))
	unlink(last_status.c_str());

      ofstream status;
      status.open(last_status.c_str());
      status << install_status;
      status.close();
    }
}

/* read cfg file to confirm treble/miui */
int TWFunc::Check_MIUI_Treble(void)
{
  string fox_is_miui_rom_installed = "0";
  string fox_is_treble_rom_installed = "0";
  string fox_is_real_treble_rom = "0";  
  Fox_Current_ROM_IsTreble = 0;
  ROM_IsRealTreble = 0;
  bool treble = false;
  string display_panel = "";
  string rom_desc = "";
  string incr_version = "";
  string finger_print = "";
  	
  // * run startup script
  RunStartupScript();
  // *

  if (TWFunc::Fox_Property_Get("orangefox.miui.rom") == "1")
  	Fox_Current_ROM_IsMIUI = 1;
  else
  	Fox_Current_ROM_IsMIUI = 0;
  	
  finger_print = TWFunc::Fox_Property_Get("orangefox.system.fingerprint");
  
  if (TWFunc::Path_Exists(orangefox_cfg)) 
    {
  	fox_is_miui_rom_installed = TWFunc::File_Property_Get (orangefox_cfg, "MIUI");
  	fox_is_treble_rom_installed = TWFunc::File_Property_Get (orangefox_cfg, "TREBLE");
  	fox_is_real_treble_rom = TWFunc::File_Property_Get (orangefox_cfg, "REALTREBLE");
  	display_panel = TWFunc::File_Property_Get (orangefox_cfg, "panel_name");	
  	Fox_Current_ROM = TWFunc::File_Property_Get (orangefox_cfg, "ROM");
  	incr_version = TWFunc::File_Property_Get (orangefox_cfg, "INCREMENTAL_VERSION");
  	if (finger_print.empty())
  	  finger_print = TWFunc::File_Property_Get (orangefox_cfg, "ROM_FINGERPRINT");
    }

   // Treble ?
   if (strncmp(fox_is_treble_rom_installed.c_str(), "1", 1) == 0)
      Fox_Current_ROM_IsTreble = 1;
        
   if (strncmp(fox_is_real_treble_rom.c_str(), "1", 1) == 0)
        ROM_IsRealTreble = 1;
   
   if (ROM_IsRealTreble == 1 || Fox_Current_ROM_IsTreble == 1)
       treble = true;
   else
       treble = Is_Real_Treble();
   
   if (treble)
      Fox_Current_ROM_IsTreble = 1;

  // is the device encrypted?
  if (StorageIsEncrypted())
      gui_print_color("accent", "* Storage is encrypted\n");
  else
      gui_print_color("warning", "* Storage is not encrypted\n");
  
  // show display panel name, if we got one 
  if (!display_panel.empty())
       gui_print("* Display: %s\n", display_panel.c_str());

  // device name
  gui_print("* Device:  %s\n", Fox_Current_Device.c_str());

  // installed ROM
  rom_desc = GetInstalledRom();
  if (!rom_desc.empty()) 
    {  
  	if (fox_is_miui_rom_installed == "1" || TWFunc::Fox_Property_Get("orangefox.miui.rom") == "1")
     	  {
  	     Fox_Current_ROM_IsMIUI = 1;
  	     gui_print("* MIUI ROM");
          } 
  	else
     	  {
  	    gui_print("* Custom ROM");
     	  } 
        gui_print("* %s\n", rom_desc.c_str());
        
        if (!incr_version.empty())
          {
  	     gui_print("* (%s)\n", incr_version.c_str());
          }

        // fingerprints
        if (!finger_print.empty())
           {
  	      gui_print("* Fingerprint: %s\n", finger_print.c_str());          
           }
    }
    else
      {
    	gui_print_color("warning", "* No ROM.\n");
      }

   gui_print("--------------------------\n");  
   return 0;
}

void TWFunc::Welcome_Message(void)
{
   if (Fox_Has_Welcomed > 0)
    return;
    gui_print("--------------------------\n");
    gui_print_color("green", "Welcome to OrangeFox Recovery!\n");
    gui_print("[Platform]  : %s\n", DataManager::GetStrValue(FOX_COMPATIBILITY_DEVICE).c_str());
    gui_print("[Release]   : %s\n", FOX_BUILD);
    gui_print("[Codebase]  : %s, %s\n", Fox_Property_Get("ro.build.version.sdk").c_str(), FOX_CURRENT_DEV_STR);
    gui_print("[Build date]: %s\n", DataManager::GetStrValue("FOX_BUILD_DATE_REAL").c_str());
    
    if (FOX_BUILD == "Unofficial")
      gui_print_color("warning", "[Build type]: Unofficial\n");
    else
    gui_print("[Build type]: %s\n", FOX_BUILD_TYPE);
#ifdef FOX_ENABLE_LAB
    gui_print_color("error", "\n*** CONFIDENTIAL ALPHA. NOT FOR RELEASE!! ***\n\n");
#endif

    gui_print("\n");
    gui_print_color("green", "OrangeFox websites:\n");
    gui_print("[Downloads] : https://orangefox.download/\n");
    gui_print("[Guides/FAQ]: https://wiki.orangefox.tech/guides/\n");
    gui_print("[App]       : https://app.orangefox.tech/\n");

    #if defined(OF_DISABLE_MIUI_SPECIFIC_FEATURES) || defined(OF_TWRP_COMPATIBILITY_MODE)
    LOGINFO(" [MIUI-specific features not enabled]\n");
    #endif
    
    gui_print("--------------------------\n");
    Fox_Has_Welcomed++;
}

void TWFunc::Fox_Set_Current_Device_CodeName(void)
{
  string tmp01 = TWFunc::Fox_Property_Get("ro.product.device");
  string currdev = DataManager::GetStrValue(FOX_COMPATIBILITY_DEVICE);
  if (!tmp01.empty() && tmp01 != currdev) {
     Fox_Current_Device = tmp01;
  }
  else Fox_Current_Device = currdev;

  DataManager::SetValue(FOX_COMPATIBILITY_DEVICE, Fox_Current_Device);
}

void TWFunc::OrangeFox_Startup(void)
{
  int i;
  std::string cpu_one, cpu_two, a, loaded_password;
  cpu_one = "/sys/devices/system/cpu/cpu";
  cpu_two = "/cpufreq/scaling_governor";
  std::string enable = "1";
  std::string disable = "0";
  std::string t2w = "/sys/android_touch/doubletap2wake";
  std::string fsync = "/sys/module/sync/parameters/fsync_enabled";
  std::string fast_charge = "/sys/kernel/fast_charge/force_fast_charge";
  std::string performance = "performance";
  std::string powersave = "powersave";
  std::string interactive = "interactive";
  std::string kernel_proc_check = "/proc/touchpanel/capacitive_keys_";
  std::string device_one = kernel_proc_check + "enable";
  std::string device_two = kernel_proc_check + "disable";

  //gui_print("DEBUG: - OrangeFox_Startup_Executed=%i\n", OrangeFox_Startup_Executed);
  
  // don't repeat this
  if (OrangeFox_Startup_Executed > 0)
     return;
  
  OrangeFox_Startup_Executed++;

  if (TWFunc::Path_Exists(FOX_PS_BIN)) 
      chmod (FOX_PS_BIN, 0755);

  Fox_Current_ROM = "";
  
  TWFunc::Welcome_Message();
  
  TWFunc::Check_MIUI_Treble();
  
  if (TWFunc::Path_Exists(device_one))
    TWFunc::write_to_file(device_one, disable);

  if (TWFunc::Path_Exists(device_two))
    TWFunc::write_to_file(device_two, enable);

  if (TWFunc::Path_Exists(t2w))
    {
      if (DataManager::GetIntValue(FOX_T2W_CHECK) == 1)
       {
	   TWFunc::write_to_file(t2w, enable);
       } 
       else
          TWFunc::write_to_file(t2w, disable);
    } 

  if (DataManager::GetIntValue(FOX_FSYNC_CHECK) == 1)
    {
      if (TWFunc::Path_Exists(fsync))
	TWFunc::write_to_file(fsync, disable);
    }

  if (DataManager::GetIntValue(FOX_FORCE_FAST_CHARGE_CHECK) == 1)
    {
      if (TWFunc::Path_Exists(fast_charge))
	{
	  TWFunc::write_to_file(fast_charge, enable);
	}
    }

  if (DataManager::GetIntValue(FOX_PERFORMANCE_CHECK) == 1)
    {
      DataManager::SetValue(FOX_GOVERNOR_STABLE, performance);
      for (i = 0; i < 9; i++)
	{
	  std::string k = to_string(i);
	  a = cpu_one + k + cpu_two;
	  if (TWFunc::Path_Exists(a))
	    TWFunc::write_to_file(a, performance);
	}
    }

  if (DataManager::GetIntValue(FOX_POWERSAVE_CHECK) == 1)
    {
      DataManager::SetValue(FOX_GOVERNOR_STABLE, powersave);
      for (i = 0; i < 9; i++)
	{
	  std::string k = to_string(i);
	  a = cpu_one + k + cpu_two;
	  if (TWFunc::Path_Exists(a))
	    TWFunc::write_to_file(a, powersave);
	}
    }

  if (DataManager::GetIntValue(FOX_BALANCE_CHECK) == 1)
    {
      DataManager::SetValue(FOX_GOVERNOR_STABLE, interactive);
      for (i = 0; i < 9; i++)
	{
	  std::string k = to_string(i);
	  a = cpu_one + k + cpu_two;
	  if (TWFunc::Path_Exists(a))
	    TWFunc::write_to_file(a, interactive);
	}
    }
  //string info = TWFunc::System_Property_Get("ro.build.display.id");
  string info = GetInstalledRom();
  if (info.empty())
    {
      LOGINFO("ROM Status: Is not installed\n");
    }
  else
    {
      LOGINFO("ROM Status: %s\n", info.c_str());
    }

  DataManager::SetValue("fox_home_files_dir", Fox_Home_Files.c_str());

  if (TWFunc::Path_Exists(FFiles_dir.c_str()))
    {
      DataManager::SetValue("fox_resource_dir", FFiles_dir.c_str());
      if (TWFunc::Path_Exists(Fox_sdcard_aroma_cfg)) // is there a backup CFG file on /sdcard/Fox/?
	{
	  TWFunc::copy_file(Fox_sdcard_aroma_cfg, Fox_aroma_cfg, 0644);
	}
      else
	{
	  if (!Path_Exists(Fox_Home))
	    {
	      if (mkdir
		  (Fox_Home.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
		{
		  LOGERR("Error making %s directory: %s\n",
			 Fox_Home.c_str(), strerror(errno));
		}
	    }         
	  if (Path_Exists(Fox_Home))
	    {
	      if (Path_Exists(Fox_aroma_cfg))
		TWFunc::copy_file(Fox_aroma_cfg, Fox_sdcard_aroma_cfg, 0644);
	    }
	} // else
    }
  else
    {
      DataManager::SetValue("fox_resource_dir", Fox_Home_Files.c_str());
    }

  if (!Path_Exists(Fox_Logs_Dir))
      {
	  TWFunc::Recursive_Mkdir(Fox_Logs_Dir, false);
      }

  TWFunc::Fresh_Fox_Install();

//==== themes version matching
  string theme_ver = DataManager::GetStrValue("of_themes_version");
  if (theme_ver.empty())
     theme_ver = "0";

  if (theme_ver == FOX_THEME_VERSION) {
     LOGINFO("Themes version: %s\n", FOX_THEME_VERSION);
  } 
  else {
     bool has_themes_dir = TWFunc::Path_Exists(Fox_Home + "/.theme");
     if (has_themes_dir)
     	gui_print_color("warning","* Themes version mismatch (old='%s'; new='%s')\n", theme_ver.c_str(), FOX_THEME_VERSION);
     else
     	LOGINFO("Themes version mismatch (old='%s'; new='%s')\n", theme_ver.c_str(), FOX_THEME_VERSION);
     
     DataManager::SetValue("of_themes_version", FOX_THEME_VERSION);
     if (has_themes_dir) {
        gui_print_color("warning", "* Resetting the themes...\n");
        TWFunc::removeDir(Fox_Home + "/.theme", false);
     }
     
     if (TWFunc::Path_Exists(Fox_Home + "/.navbar")) {
        gui_print_color("warning", "* Resetting the navbar...\n");
        TWFunc::removeDir(Fox_Home + "/.navbar", false);
     }
  }
//====

  // start mtp manually, if enabled
  #ifdef TW_HAS_MTP
 // if (DataManager::GetIntValue("tw_mtp_enabled") == 1 && !PartitionManager.is_MTP_Enabled())
 //    PartitionManager.Enable_MTP();
  #endif
}

void TWFunc::copy_kernel_log(string curr_storage)
{
  std::string dmesgDst = curr_storage + "/dmesg.log";
  std::string dmesgCmd = "/system/bin/dmesg";

  std::string result;
  Exec_Cmd(dmesgCmd, result);
  write_to_file(dmesgDst, result);
  gui_msg(Msg("copy_kernel_log=Copied kernel log to {1}") (dmesgDst));
  tw_set_default_metadata(dmesgDst.c_str());
}

void TWFunc::create_fingerprint_file(string file_path, string fingerprint)
{
  if (TWFunc::Path_Exists(file_path))
    unlink(file_path.c_str());
  ofstream file;
  file.open(file_path.c_str());
  file << fingerprint;
  file.close();
  tw_set_default_metadata(file_path.c_str());
}

bool TWFunc::Verify_Incremental_Package(string fingerprint, string metadatafp,
					string metadatadevice)
{
  string brand_property = "ro.product.brand";
  string androidversion =
    TWFunc::System_Property_Get("ro.build.version.release");
  string buildpropbrand = TWFunc::System_Property_Get(brand_property);
  string buildid = TWFunc::System_Property_Get("ro.build.id");
  string buildincremental =
    TWFunc::System_Property_Get("ro.build.version.incremental");
  string buildtags = TWFunc::System_Property_Get("ro.build.tags");
  string buildtype = TWFunc::System_Property_Get("ro.build.type");
  if (!metadatadevice.empty() && metadatadevice.size() >= 4
      && !fingerprint.empty() && fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE
      && fingerprint.find(metadatadevice) == std::string::npos)
    {
      LOGINFO("OTA_ERROR: %s\n", metadatadevice.c_str());
      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
      return false;
    }
  if (!metadatadevice.empty() && metadatadevice.size() >= 4
      && !metadatafp.empty() && metadatafp.size() > FOX_MIN_EXPECTED_FP_SIZE
      && metadatafp.find(metadatadevice) == std::string::npos)
    {
      LOGINFO("OTA_ERROR: %s\n", metadatadevice.c_str());
      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
      return false;
    }

  if (!fingerprint.empty() && fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE)
    {
      if (!buildpropbrand.empty() && buildpropbrand.size() >= 3)
	{
	  if (fingerprint.find(buildpropbrand) == std::string::npos)
	    buildpropbrand[0] = toupper(buildpropbrand[0]);
	  if (fingerprint.find(buildpropbrand) == std::string::npos)
	    buildpropbrand[0] = tolower(buildpropbrand[0]);
	  if (fingerprint.find(buildpropbrand) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildpropbrand.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
      else
	{
	  char brand[PROPERTY_VALUE_MAX];
	  property_get(brand_property.c_str(), brand, "");
	  std::string brandstr = brand;
	  if (!brandstr.empty() && brandstr.size() >= 3
	      && fingerprint.find(brandstr) == std::string::npos)
	    {
	      brandstr[0] = toupper(brandstr[0]);
	      if (!brandstr.empty() && brandstr.size() >= 3
		  && fingerprint.find(brandstr) == std::string::npos)
		brandstr[0] = tolower(brandstr[0]);
	      if (!brandstr.empty() && brandstr.size() >= 3
		  && fingerprint.find(brandstr) == std::string::npos)
		{
		  LOGINFO("OTA_ERROR: %s\n", brandstr.c_str());
		  LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
		  return false;
		}
	    }
	}
      if (!androidversion.empty() && androidversion.size() >= 3)
	{
	  if (fingerprint.find(androidversion) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", androidversion.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
      if (!buildid.empty() && buildid.size() >= 3)
	{
	  if (fingerprint.find(buildid) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildid.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
      if (!buildincremental.empty() && buildincremental.size() >= 3)
	{
	  if (fingerprint.find(buildincremental) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildincremental.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
      if (!buildtags.empty() && buildtags.size() >= 5)
	{
	  if (fingerprint.find(buildtags) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildtags.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
      if (!buildtype.empty() && buildtype.size() >= 4)
	{
	  if (fingerprint.find(buildtype) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildtype.c_str());
	      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
	      return false;
	    }
	}
    }
  if (!metadatafp.empty() && metadatafp.size() > FOX_MIN_EXPECTED_FP_SIZE)
    {
      if (!buildpropbrand.empty() && buildpropbrand.size() >= 3)
	{
	  if (metadatafp.find(buildpropbrand) == std::string::npos)
	    buildpropbrand[0] = toupper(buildpropbrand[0]);
	  if (metadatafp.find(buildpropbrand) == std::string::npos)
	    buildpropbrand[0] = tolower(buildpropbrand[0]);
	  if (metadatafp.find(buildpropbrand) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildpropbrand.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
      else
	{
	  char brandvalue[PROPERTY_VALUE_MAX];
	  property_get(brand_property.c_str(), brandvalue, "");
	  std::string brandstrtwo = brandvalue;
	  if (!brandstrtwo.empty() && brandstrtwo.size() >= 3
	      && metadatafp.find(brandstrtwo) == std::string::npos)
	    {
	      brandstrtwo[0] = toupper(brandstrtwo[0]);
	      if (!brandstrtwo.empty() && brandstrtwo.size() >= 3
		  && metadatafp.find(brandstrtwo) == std::string::npos)
		brandstrtwo[0] = tolower(brandstrtwo[0]);
	      if (!brandstrtwo.empty() && brandstrtwo.size() >= 3
		  && metadatafp.find(brandstrtwo) == std::string::npos)
		{
		  LOGINFO("OTA_ERROR: %s\n", brandstrtwo.c_str());
		  LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
		  return false;
		}
	    }
	}
      if (!androidversion.empty() && androidversion.size() >= 3)
	{
	  if (metadatafp.find(androidversion) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", androidversion.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
      if (!buildid.empty() && buildid.size() >= 3)
	{
	  if (metadatafp.find(buildid) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildid.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
      if (!buildincremental.empty() && buildincremental.size() >= 3)
	{
	  if (metadatafp.find(buildincremental) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildincremental.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
      if (!buildtags.empty() && buildtags.size() >= 5)
	{
	  if (metadatafp.find(buildtags) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildtags.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
      if (!buildtype.empty() && buildtype.size() >= 4)
	{
	  if (metadatafp.find(buildtype) == std::string::npos)
	    {
	      LOGINFO("OTA_ERROR: %s\n", buildtype.c_str());
	      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
	      return false;
	    }
	}
    }

  if (!metadatafp.empty() && metadatafp.size() > FOX_MIN_EXPECTED_FP_SIZE
      && !fingerprint.empty() && fingerprint.size() > FOX_MIN_EXPECTED_FP_SIZE
      && metadatafp != fingerprint)
    {
      LOGINFO("OTA_ERROR: %s\n", fingerprint.c_str());
      LOGINFO("OTA_ERROR: %s\n", metadatafp.c_str());
      return false;
    }
  return true;
}

bool TWFunc::Verify_Loaded_OTA_Signature(std::string loadedfp,
					 std::string ota_folder)
{
  std::string datafp;
  string ota_info = ota_folder + Fox_OTA_info;
  if (TWFunc::Path_Exists(ota_info))
    {
      if (TWFunc::read_file(ota_info, datafp) == 0)
	{
	  if (!datafp.empty() && datafp.size() > FOX_MIN_EXPECTED_FP_SIZE
	      && !loadedfp.empty()
	      && loadedfp.size() > FOX_MIN_EXPECTED_FP_SIZE
	      && datafp == loadedfp)
	    {
	      return true;
	    }
	}
    }
  return false;
}

bool TWFunc::PackRepackImage_MagiskBoot(bool do_unpack, bool is_boot)
{
  string result, tmpstr, output;
  std::string k = "/";
  std::string cd_dir = "cd ";
  std::string end_command = "; ";
  std::string cpio = "ramdisk.cpio";
  std::string tmp_cpio = Fox_tmp_dir + k + cpio;
  std::string ramdisk_cpio = Fox_ramdisk_dir + k + cpio;
  bool retval = false;
  bool keepverity = false;
  int res = 0;
  std::string cmd_script =  "/tmp/do_magisk-unpack.sh";
  std::string cmd_script2 = "/tmp/do_magisk-repack.sh";

  std::string magiskboot_sbin = Get_MagiskBoot();
  if (!TWFunc::Path_Exists(magiskboot_sbin))
     {
     	LOGERR("TWFunc::PackRepackImage_MagiskBoot: Cannot find magiskboot!");
  	TWFunc::tw_reboot(rb_recovery);
     }
 /*
  if ( (!PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path())) 
    && (!PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false)))
     {
     	LOGERR("TWFunc::PackRepackImage_MagiskBoot: Failed to mount system!");
        return false;
     }
 */
  TWPartition *Boot = PartitionManager.Find_Partition_By_Path("/boot");

#if defined(AB_OTA_UPDATER) || defined(OF_AB_DEVICE)
  if (Boot != NULL)
    {
       tmpstr = Boot->Actual_Block_Device;
#else 
  TWPartition *Recovery = PartitionManager.Find_Partition_By_Path("/recovery");
  if (Boot != NULL && Recovery != NULL)
    {
      if (is_boot)
	tmpstr = Boot->Actual_Block_Device;
      else
	tmpstr = Recovery->Actual_Block_Device;
#endif
      if (do_unpack) // unpack
	{
	  if (TWFunc::Path_Exists(Fox_tmp_dir))
	      TWFunc::removeDir(Fox_tmp_dir, false);
	    
	  if (TWFunc::Recursive_Mkdir(Fox_ramdisk_dir))
	    {
	        CreateNewFile (cmd_script);
	        chmod (cmd_script.c_str(), 0755);
	        AppendLineToFile (cmd_script, "#!/system/bin/sh");
	        AppendLineToFile (cmd_script, "LOGINFO() { echo \"$1\"; echo \"$1\" >> /tmp/recovery.log;}");
	        // if we need to backup the script, for debugging
	        if (New_Fox_Installation == 1) 
	         {
	           AppendLineToFile (cmd_script, 
	           "BackUp() { cp -f /tmp/recovery.log /sdcard/Fox/logs/post-install.log; cp -af " + cmd_script + " /sdcard/Fox/logs/cmd_script1.log; }"); 
	         }
	        else 
	           AppendLineToFile (cmd_script, "BackUp() { cp -af " + cmd_script + " /sdcard/Fox/logs/cmd_script1.log; }");
	        
	        //AppendLineToFile (cmd_script, "abort() { LOGINFO \"$1\"; BackUp; exit 1; }");
	        AppendLineToFile (cmd_script, "abort() { LOGINFO \"$1\"; exit 1; }");
	        AppendLineToFile (cmd_script, "mkdir -p " + Fox_tmp_dir);
	        AppendLineToFile (cmd_script, "mkdir -p " + Fox_ramdisk_dir);
	        AppendLineToFile (cmd_script, cd_dir + Fox_tmp_dir);
	        AppendLineToFile (cmd_script, "LOGINFO \"- Unpacking boot/recovery image - block device=\"");
	        AppendLineToFile (cmd_script, "LOGINFO \"[" + tmpstr + "] \"");
	        AppendLineToFile (cmd_script, magiskboot_sbin + " unpack -h \"" + tmpstr + "\" > /dev/null 2>&1");
	        AppendLineToFile (cmd_script, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Unpacking image failed.\"");
	        AppendLineToFile (cmd_script, "#");
	        // processing boot image?
		if (is_boot)
		   {
	              AppendLineToFile (cmd_script, cd_dir + Fox_tmp_dir);
		      std::string keepdmverity, keepforcedencryption;
		      if ((DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1)/* || (Fox_Force_Deactivate_Process == 1)*/)
		      	{
		           keepverity = false;
		           keepdmverity = "false ";
		        }
		      	else
		      	{
		           keepverity = true;
		           keepdmverity = "true ";
		        }
		      
		      	if ((DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1)/* || (Fox_Force_Deactivate_Process == 1)*/)
		      	  {
		      	     #ifdef OF_DONT_PATCH_ENCRYPTED_DEVICE
		             if (StorageIsEncrypted())
		                keepforcedencryption = "true";
		             else
		             #endif
		                keepforcedencryption = "false";
		          }
		      	else
		             keepforcedencryption = "true";
		        		
	              AppendLineToFile (cmd_script, "cp -af ramdisk.cpio ramdisk.cpio.orig");
	              AppendLineToFile (cmd_script, "LOGINFO \"- Patching ramdisk (verity/encryption) ...\"");
	              AppendLineToFile (cmd_script, magiskboot_sbin + " cpio ramdisk.cpio \"patch " + keepdmverity + keepforcedencryption + "\" > /dev/null 2>&1");
	              AppendLineToFile (cmd_script, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Ramdisk patch failed.\"");
	              AppendLineToFile (cmd_script, "rm -f ramdisk.cpio.orig");
	              if (keepverity == false)
	                 {
	              	    AppendLineToFile (cmd_script, "[ -f dtb ] && " + magiskboot_sbin + " dtb dtb patch > /dev/null 2>&1");
	              	    AppendLineToFile (cmd_script, "[ -f extra ] && " + magiskboot_sbin + " dtb extra patch > /dev/null 2>&1");	              	    
	                 }
	           } // is_boot

	        // continue processing the rest
	        AppendLineToFile (cmd_script, "#");
	        AppendLineToFile (cmd_script, "mv " + tmp_cpio + " " + ramdisk_cpio);
	        AppendLineToFile (cmd_script, cd_dir + Fox_ramdisk_dir);
	        AppendLineToFile (cmd_script, "LOGINFO \"- Extracting ramdisk files ...\"");
	        AppendLineToFile (cmd_script, magiskboot_sbin + " cpio ramdisk.cpio extract > /dev/null 2>&1");
	        AppendLineToFile (cmd_script, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Ramdisk file extraction failed.\"");
	        AppendLineToFile (cmd_script, "rm -f " + ramdisk_cpio);
	        
	        AppendLineToFile (cmd_script, "exit 0");
	        res = Exec_Cmd (cmd_script, result);
	        if (res == 0) 
	           retval = true;
	        usleep (128);
	        
		unlink(cmd_script.c_str());
	    } // if
	} // do_unpack
      else // repack
	{
	  	CreateNewFile (cmd_script2);
	  	chmod (cmd_script2.c_str(), 0755);
	        AppendLineToFile (cmd_script2, "#!/system/bin/sh");
	        AppendLineToFile (cmd_script2, "LOGINFO() { echo \"$1\"; echo \"$1\" >> /tmp/recovery.log;}");
	        // if we need to backup the script, for debugging	        
	        if (New_Fox_Installation == 1) 
	         {
	           AppendLineToFile (cmd_script2, 
	           "BackUp() { cp -af /tmp/recovery.log /sdcard/Fox/logs/post-install.log; cp -f " + cmd_script2 + " /sdcard/Fox/logs/cmd_script2.log; }"); 
	         }
	        else
	           AppendLineToFile (cmd_script2, "BackUp() { cp -f " + cmd_script2 + " /sdcard/Fox/logs/cmd_script2.log; }");

	        //AppendLineToFile (cmd_script2, "abort() { LOGINFO \"$1\"; BackUp; exit 1; }");
	        AppendLineToFile (cmd_script2, "abort() { LOGINFO \"$1\"; exit 1; }");
	        AppendLineToFile (cmd_script2, cd_dir + Fox_ramdisk_dir);
	        AppendLineToFile (cmd_script2, "LOGINFO \"- Archiving ramdisk.cpio ...\"");
	        AppendLineToFile (cmd_script2, "find | cpio -o -H newc > \"" + tmp_cpio + "\"");
	        AppendLineToFile (cmd_script2, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Archiving of ramdisk.cpio failed.\"");
	        AppendLineToFile (cmd_script2, cd_dir + Fox_tmp_dir);
	        AppendLineToFile (cmd_script2, "LOGINFO \"- Repacking boot/recovery image ...\"");
	        AppendLineToFile (cmd_script2, magiskboot_sbin + " repack \"" + tmpstr + "\" > /dev/null 2>&1");
	        AppendLineToFile (cmd_script2, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Repacking of image failed.\"");
	        AppendLineToFile (cmd_script2, "LOGINFO \"- Flashing repacked image ...\"");
	        #if defined(AB_OTA_UPDATER) || defined(OF_AB_DEVICE)
	        AppendLineToFile (cmd_script2, "dd if=new-boot.img of=" + tmpstr + " > /dev/null 2>&1");
	        #else
	        AppendLineToFile (cmd_script2, "flash_image \"" +  tmpstr + "\" new-boot.img");
	        #endif
	        AppendLineToFile (cmd_script2, "[ $? == 0 ] && LOGINFO \"- Succeeded.\" || abort \"- Flashing repacked image failed.\"");
	        AppendLineToFile (cmd_script2, magiskboot_sbin + " cleanup > /dev/null 2>&1");

	        AppendLineToFile (cmd_script2, "exit 0");
	        res = Exec_Cmd (cmd_script2, result);
		usleep (128);
		
		unlink(cmd_script2.c_str());
		
	        if (res == 0) 
	          retval = true;
	  	TWFunc::removeDir(Fox_tmp_dir, false);
	}
    } // boot != null
    else
    {
        LOGERR("TWFunc::PackRepackImage_MagiskBoot: Failed to mount boot/recovery!");
    }
  PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
  return retval;
}

bool TWFunc::isNumber(string strtocheck)
{
  int num = 0;
  std::istringstream iss(strtocheck);

  if (!(iss >> num).fail())
    return true;
  else
    return false;
}

int TWFunc::stream_adb_backup(string &Restore_Name) {
	string cmd = "/system/bin/bu --twrp stream " + Restore_Name;
	LOGINFO("stream_adb_backup: %s\n", cmd.c_str());
	int ret = TWFunc::Exec_Cmd(cmd);
	if (ret != 0)
		return -1;
	return ret;
}

void TWFunc::Read_Write_Specific_Partition(string path, string partition_name,
					   bool backup) // credits PBRP
{
  TWPartition *Partition =
    PartitionManager.Find_Partition_By_Path(partition_name);
  if (Partition == NULL || Partition->Current_File_System != "emmc")
    {
      LOGERR("Read_Write_Specific_Partition: Unable to find %s\n",
	     partition_name.c_str());
      return;
    }
  string Read_Write, oldfile, null;
  unsigned long long Remain, Remain_old;
  oldfile = path + ".bak";
  if (backup)
    Read_Write = "dump_image " + Partition->Actual_Block_Device + " " + path;
  else
    {
      Read_Write =
	"flash_image " + Partition->Actual_Block_Device + " " + path;
      if (TWFunc::Path_Exists(oldfile))
	{
	  Remain_old = TWFunc::Get_File_Size(oldfile);
	  Remain = TWFunc::Get_File_Size(path);
	  if (Remain_old < Remain)
	    {
	      return;
	    }
	}
      TWFunc::Exec_Cmd(Read_Write, null);
      return;
    }
  if (TWFunc::Path_Exists(path))
    unlink(path.c_str());
  TWFunc::Exec_Cmd(Read_Write, null);
  return;
}


string TWFunc::Load_File(string extension)
{
  string line, path = split_img + "/" + extension;
  ifstream File;
  File.open(path);
  if (File.is_open())
    {
      getline(File, line);
      File.close();
    }
  return line;
}

/* DJ9 */
std::string DataToHexString(char *data, const int len)
{
    std::stringstream ss;
    ss<<std::hex;
    for(int i(0);i<len;++i)
        ss<<(int)(data[i] & 0xff);
    return ss.str();
}

std::string GetFileHeaderMagic (string fname)
{
  FILE *f = fopen(fname.c_str(), "rb");
  char head[2];
  int len = sizeof(head);
  size_t read_len;
  if (!f)
  {
     return "00";
  }
  memset (head, 0, len);
  read_len = fread(head, 1, len, f);
  fclose (f);
  return DataToHexString(head, len);
}

#ifdef OF_USE_MAGISKBOOT_FOR_ALL_PATCHES
bool TWFunc::Repack_Image(string mount_point)
{
  bool is_boot = (mount_point == "/boot");
  return (PackRepackImage_MagiskBoot(false, is_boot));
}

bool TWFunc::Unpack_Image(string mount_point)
{
  bool is_boot = (mount_point == "/boot");
  return (PackRepackImage_MagiskBoot(true, is_boot));
}
#else // OF_USE_MAGISKBOOT_FOR_ALL_PATCHES
bool TWFunc::Unpack_Image(string mount_point)
{
  string null;

  if (TWFunc::Path_Exists(tmp))
    	TWFunc::removeDir(tmp, false);

  if (!TWFunc::Recursive_Mkdir(ramdisk))
     {
        if (!TWFunc::Path_Exists(ramdisk)) 
          {
        	LOGERR("TWFunc::Unpack_Image: Unable to create directory - \n", ramdisk.c_str());
    		return false;
    	  }
     }

  mkdir(split_img.c_str(), 0644);

  TWPartition *Partition = PartitionManager.Find_Partition_By_Path(mount_point);

  if (Partition == NULL || Partition->Current_File_System != "emmc")
    {
      LOGERR("TWFunc::Unpack_Image: Partition does not exist or is not emmc");
      return false;
    }
    
  Read_Write_Specific_Partition(tmp_boot.c_str(), mount_point, true);
  string Command = "unpackbootimg -i " + tmp + "/boot.img" + " -o " + split_img;
  if (TWFunc::Exec_Cmd(Command, null) != 0)
    {
      TWFunc::removeDir(tmp, false);
      LOGERR("TWFunc::Unpack_Image: Unpacking image failed.");
      return false;
    }
  
  string local, result, hexdump;
  DIR *dir;
  struct dirent *der;
  dir = opendir(split_img.c_str());
  if (dir == NULL)
    {
      LOGERR("TWFunc::Unpack_Image: Unable to open '%s'\n", split_img.c_str());
      return false;
    }

  while ((der = readdir(dir)) != NULL)
    {
      Command = der->d_name;
      if (Command.find("-ramdisk.") != string::npos)
	break;
    }

  closedir(dir);
  if (Command.empty())
  {
    LOGERR("TWFunc::Unpack_Image: Unpacking image failed #2.");
    return false;
  }

#ifdef OF_USE_HEXDUMP
  hexdump = "hexdump -vn2 -e '2/1 \"%x\"' " + split_img + "/" + Command;
  if (TWFunc::Exec_Cmd(hexdump, result) != 0)
    {
      TWFunc::removeDir(tmp, false);
      LOGERR("TWFunc::Unpack_Image: Command failed '%s'\n", hexdump.c_str());
      return false;
    }
#else
  result = GetFileHeaderMagic (split_img + "/" + Command);
  hexdump = "GetFileHeaderMagic(" + split_img + "/" + Command + ")";
  if (result == "00")
    {
      TWFunc::removeDir(tmp, false);
      LOGERR("TWFunc::Unpack_Image: Command failed '%s'\n", hexdump.c_str());
      return false;
    }
#endif
    
  //LOGINFO("TWFunc::Unpack_Image: Running Command: '%s' and result='%s'\n", hexdump.c_str(), result.c_str()); // !!
  if (result == "425a")
    local = "bzip2 -dc";
  else if (result == "1f8b" || result == "1f9e")
    local = "gzip -dc";
  else if (result == "0221")
    local = "lz4 -d";
  else if (result == "5d00" || result == "5d0")
    local = "lzma -dc";
  else if (result == "894c")
    local = "lzop -dc";
  else if (result == "fd37")
    local = "xz -dc";
  else
   {
    LOGERR("TWFunc::Unpack_Image: the command %s yields an unknown compression type.\n", hexdump.c_str());
    return false;
   }
       
  result = "cd " + ramdisk + "; " + local + " < " + split_img + "/" + Command + " | cpio -i";
  null = Exec_With_Output (result);
  if (null.empty())
     return true;
  
  if (null == exec_error_str)
     {
        LOGERR("TWFunc::Unpack_Image: Command failed '%s'\n", result.c_str());
        TWFunc::removeDir(tmp, false);
        return false;    
     }
  else
     {
        LOGINFO("TWFunc::Unpack_Image: output of command:'%s' was:\n'%s'\n", result.c_str(), null.c_str());
        /*
        TWFunc::removeDir(tmp, false);
        return false;
        */
      }
  return true;
}

bool TWFunc::Repack_Image(string mount_point)
{
  string null, local, result, hexdump, Command;
  DIR *dir;
  struct dirent *der;
  
  dir = opendir(split_img.c_str());
  if (dir == NULL)
    {
      LOGINFO("Unable to open '%s'\n", split_img.c_str());
      return false;
    }
  
  while ((der = readdir(dir)) != NULL)
    {
      local = der->d_name;
      if (local.find("-ramdisk.") != string::npos)
	break;
    }
  
  closedir(dir);
  if (local.empty())
  {
    LOGERR("TWFunc::Repack_Image: -ramdisk. not found in \n", split_img.c_str());
    return false;
   }
    
#ifdef OF_USE_HEXDUMP
  hexdump = "hexdump -vn2 -e '2/1 \"%x\"' " + split_img + "/" + local;
  TWFunc::Exec_Cmd(hexdump, result);
#else
  result = GetFileHeaderMagic (split_img + "/" + local);
  hexdump = "GetFileHeaderMagic(" + split_img + "/" + local + ")";
#endif
  //LOGINFO("TWFunc::Repack_Image: Running Command: '%s' and result='%s'\n", hexdump.c_str(), result.c_str()); // !!
  if (result == "425a")
    local = "bzip2 -9c";
  else if (result == "1f8b" || result == "1f9e")
    local = "gzip -9c";
  else if (result == "0221")
    local = "lz4 -9";
  else if (result == "5d00" || result == "5d0")
    local = "lzma -6c";
  else if (result == "894c")
    local = "lzop -9c";
  else if (result == "fd37")
    local = "xz --check=crc32 --lzma2=dict=2MiB";
  else 
  {
    LOGERR("TWFunc::Repack_Image: the command %s yields an unknown compression type.\n", hexdump.c_str());
    return false;
  }
  
  string repack = "cd " + ramdisk + "; find | cpio -o -H newc | " + local + " > " + tmp + "/ramdisk-new";
  //LOGINFO("TWFunc::Repack_Image: Running Command: '%s'\n", repack.c_str());  // !!
  TWFunc::Exec_Cmd(repack, null);
  if (null == exec_error_str)
     {
        LOGERR("TWFunc::Repack_Image: Command failed '%s'\n", repack.c_str());
     }
  else
  if (!null.empty())
     {
        LOGINFO("TWFunc::Repack_Image: output of command:'%s' was:\n'%s'\n", repack.c_str(), null.c_str());
     }

  dir = opendir(split_img.c_str());
  if (dir == NULL)
    {
      LOGINFO("Unable to open '%s'\n", split_img.c_str());
      return false;
    }
  Command = "mkbootimg";
  while ((der = readdir(dir)) != NULL)
    {
      local = der->d_name;
      if (local.find("-zImage") != string::npos)
	{
	  Command += " --kernel " + split_img + "/" + local;
	  continue;
	}
      if (local.find("-ramdisk.") != string::npos)
	{
	  Command += " --ramdisk " + tmp + "/ramdisk-new";
	  continue;
	}
      if (local.find("-dtboff") != string::npos)
	{
	  Command += " --dtb_offset 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-dtb") != string::npos)
	{
	  Command += " --dtb " + split_img + "/" + local;
	  continue;
	}
      if (local == "boot.img-second")
	{
	  Command += " --second " + split_img + "/" + local;
	  continue;
	}
      if (local.find("-cmdline") != string::npos)
	{
	  Command += " --cmdline \"" + TWFunc::Load_File(local) + "\"";
	  continue;
	}
      if (local.find("-board") != string::npos)
	{
	  Command += " --board \"" + TWFunc::Load_File(local) + "\"";
	  continue;
	}
      if (local.find("-pagesize") != string::npos)
	{
	  Command += " --pagesize " + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-headerversion") != string::npos)
	{
	  Command += " --header_version " + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-base") != string::npos)
	{
	  Command += " --base 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-secondoff") != string::npos)
	{
	  Command += " --second_offset 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-kerneloff") != string::npos)
	{
	  Command += " --kernel_offset 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-ramdiskoff") != string::npos)
	{
	  Command += " --ramdisk_offset 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-tagsoff") != string::npos)
	{
	  Command += " --tags_offset 0x" + TWFunc::Load_File(local);
	  continue;
	}
      if (local.find("-hash") != string::npos)
	{
	  if (Load_File(local) == "unknown")
	    Command += " --hash sha1";
	  else
	    Command += " --hash " + Load_File(local);
	  continue;
	}
      if (local.find("-recoverydtbo") != string::npos)
	{
	  Command += " --recovery_dtbo " + split_img + "/" + local;
	  continue;
	}

      if (local.find("-recoveryacpio") != string::npos)
	{
	  Command += " --recovery_acpio " + split_img + "/" + local;
	  continue;
	}

      if (local.find("-osversion") != string::npos)
	{
	  Command += " --os_version \"" + Load_File(local) + "\"";
	  continue;
	}
      if (local.find("-oslevel") != string::npos)
	{
	  Command += " --os_patch_level \"" + Load_File(local) + "\"";
	  continue;
	}
    }
  closedir(dir);
  Command += " --output " + tmp_boot;
  string bk1 = tmp_boot + ".bak";
  rename(tmp_boot.c_str(), bk1.c_str());  
  if (TWFunc::Exec_Cmd(Command, null) != 0)
    {
      TWFunc::removeDir(tmp, false);
      LOGERR("TWFunc::Repack_Image: the command %s was unsuccessful.\n", Command.c_str());
      return false;
    }
  //if (!null.empty()) LOGINFO("TWFunc::Repack_Image: output of final command:'%s' was:\n'%s'\n", Command.c_str(), null.c_str()); //!!
  char brand[PROPERTY_VALUE_MAX];
  property_get("ro.product.manufacturer", brand, "");
  hexdump = brand;
  if (!hexdump.empty())
    {
      for (size_t i = 0; i < hexdump.size(); i++)
	hexdump[i] = tolower(hexdump[i]);
      if (hexdump == "samsung")
	{
	  ofstream File(tmp_boot.c_str(), ios::binary);
	  if (File.is_open())
	    {
	      File << "SEANDROIDENFORCE" << endl;
	      File.close();
	    }
	}
     //LOGINFO("TWFunc::Repack_Image: Manufacturer='%s'\n", hexdump.c_str());  // !! 
    }
  Read_Write_Specific_Partition(tmp_boot.c_str(), mount_point, false);
  TWFunc::removeDir(tmp, false);
  return true;
}
#endif // OF_USE_MAGISKBOOT_FOR_ALL_PATCHES

bool TWFunc::JustInstalledMiui(void)
{
  Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
  if ((Fox_Zip_Installer_Code == 22) || (Fox_Zip_Installer_Code == 23) 
  || (Fox_Zip_Installer_Code == 3) || (Fox_Zip_Installer_Code == 2)) {
      	TWFunc::Fox_Property_Set("orangefox.miui.rom", "1");
      	return true;
      }
  else
      return false;
}

bool TWFunc::Fresh_Fox_Install()
{
  std::string fox_file = get_log_dir() + "recovery/Fox_Installed";
  bool CanProceed = true;
  New_Fox_Installation = 0;

  if (get_log_dir() == CACHE_LOGS_DIR)
    {
      CanProceed = (PartitionManager.Is_Mounted_By_Path(CACHE_LOGS_DIR) 
                 || PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, true));
    }

  if (CanProceed)
    {
	if (!Path_Exists(fox_file))
	    return false;

	unlink(fox_file.c_str());
	
  	DataManager::SetValue("first_start", "1");
  	DataManager::SetValue("of_themes_version", FOX_THEME_VERSION);

	#ifdef OF_QUICK_BACKUP_LIST
  	DataManager::SetValue("tw_backup_list_quick", OF_QUICK_BACKUP_LIST);
	#endif

	#ifdef OF_DONT_PATCH_ON_FRESH_INSTALLATION
	    gui_print("Fresh OrangeFox installation - not running the dm-verity/forced-encryption patches\n");
	#else
	    New_Fox_Installation = 1;
	    gui_print("Fresh OrangeFox installation - about to run the dm-verity/forced-encryption patches\n");
     	    if (Fox_Current_ROM_IsMIUI == 1)
     	       {
		  Fox_Force_Deactivate_Process = 1;
		  DataManager::SetValue(FOX_FORCE_DEACTIVATE_PROCESS, 1);
	       }
	    TWFunc::Deactivation_Process();
	    usleep(16384);
	    TWFunc::Patch_AVB20(false);
	    usleep(16384);
	    New_Fox_Installation = 0;
	#endif // OF_DONT_PATCH_ON_FRESH_INSTALLATION

	LOGINFO ("DEBUG [Fresh_Fox_Install()] - copying log to:/sdcard/Fox/logs/post-install.log \n");
	copy_file("/tmp/recovery.log", "/sdcard/Fox/logs/post-install.log", 0644);

	return true;
   }    
   else
   {
      	if (Path_Exists(fox_file)) {
      	  unlink(fox_file.c_str());
      	  DataManager::SetValue("of_themes_version", FOX_THEME_VERSION);
      	}
      	return false;
   }
}

bool TWFunc::Patch_DM_Verity(void)
{
  bool status = false;
  bool found_verity = false;
  DIR *d = NULL;
  struct dirent *de = NULL;
  string path, cmp;
  string firmware_key = ramdisk + Fox_Bin_Dir + "/firmware_key.cer";
  string remove = "verify,;,verify;verify;avb,;,avb;avb;support_scfs,;,support_scfs;support_scfs;";

  LOGINFO("OrangeFox: entering Patch_DM_Verity()\n");
  if (DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) != 1)
    {
	gui_print("OrangeFox: Not patching DM_Verity.\n");  
	return false;
    }

  // /tmp/orangefox/ramdisk
  d = opendir(ramdisk.c_str());
  if (d == NULL)
    {
      LOGINFO("Unable to open '%s'\n", ramdisk.c_str());
      return false;
    }
    
  while ((de = readdir(d)) != NULL)
    {
      usleep (32);
      cmp = de->d_name;
      path = ramdisk + "/" + cmp;
      if (cmp.find("fstab.") != string::npos)
	{
	  //gui_msg(Msg("of_fstab=Detected fstab: '{1}'") (cmp));
	  if (!status)
	    {
	      if (Fstab_Has_Verity_Flag(path))
	        {
	          LOGINFO("OrangeFox: Relevant DM_Verity flags are found in %s\n", path.c_str());
		  status = true;
		  found_verity = true;
	  	  TWFunc::Replace_Word_In_File(path, remove);
		}
		else
		{
	          LOGINFO("OrangeFox: Relevant DM_Verity flags are not found in %s\n", path.c_str());		
		}
	    }
	}
	
      if (cmp == "default.prop")
	{
	  if (TWFunc::CheckWord(path, "ro.config.dmverity="))
	    {
                LOGINFO("OrangeFox: DM_Verity flags found in default.prop.\n");
		found_verity = true;
	        if (TWFunc::CheckWord(path, "ro.config.dmverity=true"))
	           {
			status = true;
			TWFunc::Patch_Verity_Flags(path);
		   }			     
	    }
	  else
	    {
              LOGINFO("OrangeFox: DM_Verity flags not found in default.prop.\n");
	      ofstream File(path.c_str(), ios_base::app | ios_base::out);
	      if (File.is_open())
		{
		  File << "ro.config.dmverity=false" << endl;
		  File.close();
		}
	    }
	}
	
      if (cmp == "verity_key")
	{
	  if (!status)
	    status = true;
	  unlink(path.c_str());
	}
    }
  closedir(d);

  if (TWFunc::Path_Exists(firmware_key))
    {
      if (!status)
	status = true;
      unlink(firmware_key.c_str());
    }

  #ifndef OF_USE_MAGISKBOOT
  if ((status == true) && (found_verity == false))
    {
       LOGINFO("OrangeFox: Partial success - DM-Verity settings not found in fstab, but key file was successfully removed.\n");
    }
    
  if (found_verity == false && status == false && JustInstalledMiui() == true)
     {
         LOGINFO("OrangeFox: Dm-verity not patched. This MIUI ROM might not boot without flashing magisk.\n");
         gui_print_color("warning", "\nI could not patch dm-verity.\nTry flashing magisk from the OrangeFox menu now!\n");
     } 
  #endif
       
  LOGINFO("OrangeFox: leaving Patch_DM_Verity()\n");
  return status;
}


void TWFunc::Patch_Verity_Flags(string path)
{
   TWFunc::Replace_Word_In_File(path, "ro.config.dmverity=true;", "ro.config.dmverity=false");
   usleep(64000); 
   if (TWFunc::CheckWord(path, "ro.config.dmverity=true"))
   {
      string root = Get_Root_Path (path);
      if ((root == "/vendor") || (root == PartitionManager.Get_Android_Root_Path()))
      {
        LOGINFO("OrangeFox: Patch_Encryption_Flags: trying again...\n");
	int res;
	string result;
	string cmd_script = "/tmp/dmver.sh";
   	CreateNewFile (cmd_script);
   	chmod (cmd_script.c_str(), 0755);
   	AppendLineToFile (cmd_script, "#!/system/bin/sh");
   	AppendLineToFile (cmd_script, "mount -o rw,remount " + root);
   	AppendLineToFile (cmd_script, "mount -o rw,remount " + root + " " + root);
   	AppendLineToFile (cmd_script, "sed -i -e \"s|ro.config.dmverity=true|ro.config.dmverity=false|g\" " + path);
   	AppendLineToFile (cmd_script, "umount " + root + " > /dev/null 2>&1");
    	//AppendLineToFile (cmd_script, "chmod 0644 " + path);  	
   	AppendLineToFile (cmd_script, "");
   	AppendLineToFile (cmd_script, "exit 0");
   	res = Exec_Cmd (cmd_script, result);
   	unlink(cmd_script.c_str());
      }    
  }
}

bool TWFunc::Fstab_Has_Verity_Flag(std::string path)
{
    if (
       (TWFunc::CheckWord(path, "verify")) 
    || (TWFunc::CheckWord(path, "support_scfs"))
    || (TWFunc::CheckWord(path, "avb"))
       )
        return true;
   else
        return false;
}

bool TWFunc::Fstab_Has_Encryption_Flag(std::string path)
{
   if (
        (CheckWord(path, "forceencrypt")) 
     || (CheckWord(path, "forcefdeorfbe"))
     || (CheckWord(path, "fileencryption"))
//     || (CheckWord(path, "errors=panic")) 
//     || (CheckWord(path, "discard"))
      )
        return true;
   else
        return false;
}

/* patch the /system || /vendor fstab */
bool Patch_DM_Verity_In_System_Fstab(void)
{
  bool status = false;
  bool found_verity = false;
  bool def = false;
  int stat = 0;
  int verity = 0;
  DIR *d1 = NULL;
  struct dirent *de = NULL;
  string path, cmp, command;
  string firmware_key = ramdisk + Fox_Bin_Dir + "/firmware_key.cer";
  string remove = "verify,;,verify;verify;avb,;,avb;avb;support_scfs,;,support_scfs;support_scfs;";

      DataManager::GetValue(FOX_DISABLE_DM_VERITY, verity);
      if (verity != 1)
       {
          gui_print ("OrangeFox: 'Disable DM-Verity' not enabled.\n");
          return false;
       }
  
      d1 = NULL;
      if (Treble_Is_Running())
      {
          if ((PartitionManager.Is_Mounted_By_Path("/vendor")) || (PartitionManager.Mount_By_Path("/vendor", false)))
	   {
	      d1 = opendir(fstab2.c_str());
	      stat = 2;
	      if (d1 == NULL) 
	        {
		  LOGINFO ("DEBUG: DM-Verity: Hmmm ... /vendor is mounted, but I can't open '%s'. This should not happen.\n", fstab2.c_str()); //!!
		  gui_print ("- DM-Verity #1: Has 'someone' been wiping things, and then flashing a ROM without first rebooting the recovery?\n");//!!
		  if (TWFunc::Path_Exists(fstab2)) 
		    {
			LOGINFO ("DEBUG: DM-Verity: '%s' actually exists!\n", fstab2.c_str()); //!!
		    }
		  else 
		    {
			LOGINFO ("DEBUG: DM-Verity: '%s' does not exist!\n", fstab2.c_str()); //!!			
		    }		
	        }
	   } 
      } 

     if (d1 == NULL)
	{
          if ((PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path())) 
          || (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false)))
	     {
	        d1 = opendir(fstab1.c_str());
	        stat = 1;
	        if (d1 == NULL)
	           {
			LOGINFO ("DEBUG: DM-Verity: Hmmm ... /system is mounted, but I can't open '%s'. This should not happen.\n", fstab1.c_str());//!!
			gui_print ("- DM-Verity #2: Has 'someone' been wiping things, and then flashing a ROM without first rebooting the recovery?\n");//!!
			if (TWFunc::Path_Exists(fstab1)) 
			  {
		  	    LOGINFO ("DEBUG: DM-Verity: '%s' actually exists!\n", fstab1.c_str());//!!
			  } 
			else 
			  {
			    LOGINFO ("DEBUG: DM-Verity: '%s' does not exist!\n", fstab1.c_str());//!!		
			  }
	           }
	     }
        }
 
      if (d1 == NULL)
        {
	    #ifndef OF_USE_MAGISKBOOT
	    gui_print ("OrangeFox: DM-Verity not patched in system fstab - cannot mount either /system or /vendor. Reboot OrangeFox and try again.\n");
	    #endif 
	    if (stat == 2)
		LOGINFO("Unable to open '%s'\n", fstab2.c_str());
	    else 
	    if (stat == 1)
		LOGINFO("Unable to open '%s'\n", fstab1.c_str());
		
	    return false;
        }
     
      while ((de = readdir(d1)) != NULL)
	{
	  usleep (32);
	  cmp = de->d_name;
	  
	  if (stat == 2)
	        path = fstab2 + "/" + cmp;
	  else 
	  if (stat == 1)
	        path = fstab1 + "/" + cmp;
	  
	  if (cmp.find("fstab.") != string::npos)
	    {
	      //gui_msg(Msg("of_fstab=Detected fstab: '{1}'") (cmp));
	      if (!status)
		{
		  if (TWFunc::Fstab_Has_Verity_Flag(path))
		    {
	               LOGINFO("OrangeFox: Relevant dm-verity settings are found in %s\n", path.c_str());
		       status = true;
		       found_verity = true;
	      	       TWFunc::Replace_Word_In_File(path, remove);
		    } 
		    else
		    {
	               LOGINFO("OrangeFox: Relevant dm-verity settings are not found in %s\n", path.c_str());		    
		    }
		}
	    }
	  if (cmp == "default.prop")
	    {
	      def = true;
	      if (TWFunc::CheckWord(path, "ro.config.dmverity="))
		{
		    found_verity = true;
		    if (TWFunc::CheckWord(path, "ro.config.dmverity=true"))
		       {
	    	    	   status = true;
		    	   TWFunc::Patch_Verity_Flags(path);
		       }
		}
		else
		{
		  ofstream File(path.c_str(), ios_base::app | ios_base::out);
		  if (File.is_open())
		     {
		        File << "ro.config.dmverity=false" << endl;
		        File.close();
		        status = true;
		     }
		}
	    }  // default.prop
	} // while      
      closedir(d1);
      
      //additional check for default.prop
      if (!def)
	{
	  if (PartitionManager.Is_Mounted_By_Path("/vendor"))
	    path = fstab2 + "/default.prop";
	  else
	    path = fstab1 + "/default.prop";
	    
	  if (TWFunc::CheckWord(path, "ro.config.dmverity=true"))
	     {
		status = true;
		found_verity = true;
		TWFunc::Patch_Verity_Flags(path);
	     }
	} // !def

      if (New_Fox_Installation != 1)
         {
      		if (PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path()))
	  		PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
	
       		if (PartitionManager.Is_Mounted_By_Path("/vendor"))
	  		PartitionManager.UnMount_By_Path("/vendor", false);
	 }
	 
      return status;
}


void TWFunc::Patch_Encryption_Flags(std::string path)
{
   LOGINFO("OrangeFox: Patch_Encryption_Flags: processing file:%s\n", path.c_str());
   TWFunc::Replace_Word_In_File(path, "fileencryption=ice;", "encryptable=footer");
   TWFunc::Replace_Word_In_File(path, "forcefdeorfbe=;forceencrypt=;fileencryption=;", "encryptable=");
   usleep(64000); 
   if (Fstab_Has_Encryption_Flag(path))
   {
      string root = Get_Root_Path (path);
      if ((root == "/vendor") || (root == PartitionManager.Get_Android_Root_Path()))
      {
        LOGINFO("OrangeFox: Patch_Encryption_Flags: trying again...\n");
	int res;
	string result;
	string cmd_script = "/tmp/fenc.sh";
   	CreateNewFile (cmd_script);
   	chmod (cmd_script.c_str(), 0755);
   	AppendLineToFile (cmd_script, "#!/system/bin/sh");
   	AppendLineToFile (cmd_script, "mount -o rw,remount " + root);
   	AppendLineToFile (cmd_script, "mount -o rw,remount " + root + " " + root);
   	AppendLineToFile (cmd_script, "sed -i -e \"s|fileencryption=ice|encryptable=footer|g\" " + path);
   	AppendLineToFile (cmd_script, "sed -i -e \"s|forcefdeorfbe=|encryptable=|g\" " + path);
   	AppendLineToFile (cmd_script, "sed -i -e \"s|forceencrypt=|encryptable=|g\" " + path);
   	AppendLineToFile (cmd_script, "sed -i -e \"s|fileencryption=|encryptable=|g\" " + path);
   	AppendLineToFile (cmd_script, "umount " + root + " > /dev/null 2>&1");
   	AppendLineToFile (cmd_script, "");
   	AppendLineToFile (cmd_script, "exit 0");
   	res = Exec_Cmd (cmd_script, result);
   	unlink(cmd_script.c_str());
      }    
   }
//   string remove = "errors=panic,;errors=panic;discard,;,discard;";
//   TWFunc::Replace_Word_In_File(path, remove);
}

/* patch the /sytem || /vendor fstab for forced-encryption - only if we are not already encrypted */
bool Patch_Forced_Encryption_In_System_Fstab(void)
{
  string path = "";
  string cmp = "";
  int stat = 0;
  bool status = false;
  int encryption = 0;
  DIR *d1 = NULL;
  struct dirent *de;
  
      DataManager::GetValue(FOX_DISABLE_FORCED_ENCRYPTION, encryption);
      if (encryption != 1)
         {
            gui_print ("OrangeFox: 'Disable Forced-Encryption' not enabled.\n");
            return false;
         }
  
      #ifdef OF_DONT_PATCH_ENCRYPTED_DEVICE
      if (StorageIsEncrypted())
        {
            gui_print ("OrangeFox: Storage is encrypted. Not patching system fstab.\n");
            return false;
        }
      #endif
  
      d1 = NULL;
      if (Treble_Is_Running())
      {
         if ((PartitionManager.Is_Mounted_By_Path("/vendor")) || (PartitionManager.Mount_By_Path("/vendor", false)))
	   {
	      d1 = opendir(fstab2.c_str());
	      stat = 2;
	     
	      if (d1 == NULL) 
	      {
		LOGINFO ("DEBUG: Forced-Encryption: Hmmm ... /vendor is mounted, but I can't open '%s'. This should not happen.\n", fstab2.c_str()); //!!
		gui_print ("- Forced-Encryption #1: Has 'someone' been wiping things, and then flashing a ROM without first rebooting the recovery?\n");//!!
		if (TWFunc::Path_Exists(fstab2)) 
		  {
			LOGINFO ("DEBUG: Forced-Encryption: '%s' actually exists!\n", fstab2.c_str()); //!!
		  }
		else 
		  {
			LOGINFO ("DEBUG: Forced-Encryption: '%s' does not exist!\n", fstab2.c_str()); //!!			
		  }		
	      }	      
	   }
      } 

     if (d1 == NULL)
	{
          if ((PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path())) 
          || (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false)))
	     {
	        d1 = opendir(fstab1.c_str());
	        stat = 1;
	        if (d1 == NULL) 
	        {
		  LOGINFO ("DEBUG: Forced-Encryption: Hmmm ... /system is mounted, but I can't open '%s'. This should not happen.\n", fstab1.c_str()); //!!
		  gui_print ("- Forced-Encryption #2: Has 'someone' been wiping things, and then flashing a ROM without first rebooting the recovery?\n");//!!
		  if (TWFunc::Path_Exists(fstab2)) 
		    {
			LOGINFO ("DEBUG: Forced-Encryption: '%s' actually exists!\n", fstab1.c_str()); //!!
		    }
		  else 
		    {
			LOGINFO ("DEBUG: Forced-Encryption: '%s' does not exist!\n", fstab1.c_str()); //!!			
		    }		
	        }
	     }
        }
 
      if (d1 == NULL)
        {
	    gui_print ("OrangeFox: Forced-Encryption not patched in system fstab - cannot mount either /system or /vendor. Reboot OrangeFox and try again.\n");
	    if (stat == 2)
		LOGINFO("Unable to open '%s'\n", fstab2.c_str());
	    else 
	    if (stat == 1)
		LOGINFO("Unable to open '%s'\n", fstab1.c_str());
	    return false;
        }
           
      while ((de = readdir(d1)) != NULL)
	{
	  usleep (32);
	  cmp = de->d_name;
	  if (stat == 2)
 	       path = fstab2 + "/" + cmp;
	  else 
	  if (stat == 1)
	       path = fstab1 + "/" + cmp;
	  if (cmp.find("fstab.") != string::npos)
	    {
	     if (!status)
	        {
	          if (TWFunc::Fstab_Has_Encryption_Flag(path))
	          {
		      LOGINFO("OrangeFox: Relevant encryption settings are found in %s\n", path.c_str());
		      status = true;
		  } 
		  else
		  { 
		      LOGINFO("OrangeFox: Relevant encryption settings are not found in %s\n", path.c_str());
		  }
	       }
	     
	     if (TWFunc::Fstab_Has_Encryption_Flag(path))
	       {
	          status = true;
	          TWFunc::Patch_Encryption_Flags(path);
	       }
	     
	   }
	} // while
      closedir(d1);
      if (New_Fox_Installation != 1)
         {
       		if (PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path()))
    	      		PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
    
       		if (PartitionManager.Is_Mounted_By_Path("/vendor"))
    	      		PartitionManager.UnMount_By_Path("/vendor", false);
    	  }
  return status;
}

bool TWFunc::Patch_Forced_Encryption(void)
{
  string path, cmp;
  bool status = false;
  DIR *d;
  struct dirent *de;
  
  LOGINFO("OrangeFox: entering Patch_Forced_Encyption()\n");
  if (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) != 1)
  {
  	gui_print("OrangeFox: Not patching forced encryption.\n");
  	return false;  
  }
  
  #ifdef OF_DONT_PATCH_ENCRYPTED_DEVICE
  if (StorageIsEncrypted())
    {
  	gui_print("OrangeFox: Storage is already encrypted. Not patching the boot image.\n");
  	return false;	     
    }
  #endif
   
  d = opendir(ramdisk.c_str());
  if (d == NULL)
    {
      LOGINFO("Unable to open '%s'\n", ramdisk.c_str());
      return false;
    }

  //*** /tmp/orangefox/ramdisk/    
  while ((de = readdir(d)) != NULL)
    {
      usleep (32);
      cmp = de->d_name;
      path = ramdisk + "/" + cmp;
      
      if (cmp.find("fstab.") != string::npos)
	{
	  if (!status)
	    {
	      if (Fstab_Has_Encryption_Flag(path))
	      {  
		  LOGINFO("OrangeFox: Relevant encryption settings are found in %s\n", path.c_str());
		  status = true;
	      }
	      else
	      {
		  LOGINFO("OrangeFox: Relevant encryption settings are not found in %s\n", path.c_str());
	      }
	    }
	  if (Fstab_Has_Encryption_Flag(path))
	     {
	          status = true;
	          TWFunc::Patch_Encryption_Flags(path);
	     }
	}
    } // while
    
  closedir(d);  

  LOGINFO("OrangeFox: leaving Patch_Forced_Encyption()\n");
  return status;
}

void TWFunc::PrepareToFinish(void)
{
   // unmount stuff
   if (PartitionManager.Is_Mounted_By_Path("/vendor"))
	PartitionManager.UnMount_By_Path("/vendor", false);
   //else 
   if (PartitionManager.Is_Mounted_By_Path("/cust"))
	PartitionManager.UnMount_By_Path("/cust", false);
  
   if (PartitionManager.Is_Mounted_By_Path(PartitionManager.Get_Android_Root_Path()))
     PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
  //
  
  Fox_Zip_Installer_Code = DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE);
  Fox_Force_Deactivate_Process = DataManager::GetIntValue(FOX_FORCE_DEACTIVATE_PROCESS);

  // increment value, to show how many times we have called this
  Fox_IsDeactivation_Process_Called++;

  // Check AromaFM Config
  if (
     (DataManager::GetIntValue(FOX_SAVE_LOAD_AROMAFM) == 1)
  && (PartitionManager.Mount_By_Path("/sdcard", false))
     )
    {
      string aromafm_path = Fox_Home;
      string aromafm_file = aromafm_path + "/aromafm.cfg";
      if (!Path_Exists(aromafm_path))
	{
	  if (mkdir
	      (aromafm_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
	    {
	      LOGERR("Error creating %s directory: %s\n", aromafm_path.c_str(), strerror(errno));
	    }
	}

      // Save AromaFM config (AromaFM.cfg)
      if (Path_Exists(Fox_aroma_cfg))
      	 {
      	   if (copy_file(Fox_aroma_cfg, aromafm_file, 0644))
	      {
	         LOGERR("Error copying AromaFM config\n");
	      }
      	 }
      PartitionManager.UnMount_By_Path("/sdcard", false);
    }

  // restore the stock recovery ?
  #ifndef OF_VANILLA_BUILD
  if (
     (DataManager::GetIntValue(FOX_DONT_REPLACE_STOCK) == 1)
  && (PartitionManager.Mount_By_Path(PartitionManager.Get_Android_Root_Path(), false))
     )
    {
      if (Path_Exists("/system/wlfx0recovery-from-boot.bak0xwlf"))
	{
	  rename("/system/wlfx0recovery-from-boot.bak0xwlf",
		 "/system/recovery-from-boot.p");
	}
      else if (Path_Exists("/system/system/wlfx0recovery-from-boot.bak0xwlf"))
	{
	  rename("/system/system/wlfx0recovery-from-boot.bak0xwlf",
		 "/system/system/recovery-from-boot.p");
	}
	
      PartitionManager.UnMount_By_Path(PartitionManager.Get_Android_Root_Path(), false);
    }
   #endif
}

bool TWFunc::DontPatchBootImage(void)
{
  // check whether to patch on new OrangeFox installations 
  if (New_Fox_Installation == 1)
     { 
        if (Is_AB_Device()) // don't patch the boot image of A/B devices at post-install stage
        {
           return true;
        }

        if ((DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) != 1) 
        && (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) != 1))
           {  // if we get here, the user has turned off these settings manually
            #if defined(OF_FORCE_MAGISKBOOT_BOOT_PATCH_MIUI) && defined(OF_USE_MAGISKBOOT)
  	    if (MIUI_Is_Running())
  	       {
     	  	  LOGINFO("OrangeFox: Fresh OrangeFox installation on MIUI. Processing will continue...\n");
     	  	  return false;
  	       }
	    #endif
            return true;
           }
     }

   // proceed with other checks
   Fox_Force_Deactivate_Process = DataManager::GetIntValue(FOX_FORCE_DEACTIVATE_PROCESS);
   if (
          (Fox_Force_Deactivate_Process == 1) || 
          (DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1) || 
          (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1)
      )
      return false;
   else
     {
        #if defined(OF_FORCE_MAGISKBOOT_BOOT_PATCH_MIUI) && defined(OF_USE_MAGISKBOOT)
  	if (TWFunc::JustInstalledMiui())
  	   {
     	      LOGINFO("OrangeFox: New MIUI installation. Processing will continue...\n");
     	      return false;
  	   }
	#endif
        return true;
     }
}

std::string TWFunc::get_log_dir() {
	if (PartitionManager.Find_Partition_By_Path(CACHE_LOGS_DIR) == NULL) {
		if (PartitionManager.Find_Partition_By_Path(DATA_LOGS_DIR) == NULL) {
			LOGINFO("Unable to find a directory to store OrangeFox logs.");
			return "";
		} else {
			return DATA_LOGS_DIR;
		}
	}
	else {
		return CACHE_LOGS_DIR;
	}
}

void TWFunc::check_selinux_support() {
	if (TWFunc::Path_Exists("/prebuilt_file_contexts")) {
		if (TWFunc::Path_Exists("/file_contexts")) {
			printf("Renaming regular /file_contexts -> /file_contexts.bak\n");
			rename("/file_contexts", "/file_contexts.bak");
		}
		printf("Moving /prebuilt_file_contexts -> /file_contexts\n");
		rename("/prebuilt_file_contexts", "/file_contexts");
	}
	struct selinux_opt selinux_options[] = {
		{ SELABEL_OPT_PATH, "/file_contexts" }
	};
	selinux_handle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
	if (!selinux_handle)
		printf("No file contexts for SELinux\n");
	else
		printf("SELinux contexts loaded from /file_contexts\n");
	{ // Check to ensure SELinux can be supported by the kernel
		char *contexts = NULL;
		std::string cacheDir = TWFunc::get_log_dir();
		std::string se_context_check = cacheDir + "recovery/";
		int ret = 0;

		if (cacheDir == CACHE_LOGS_DIR) {
			PartitionManager.Mount_By_Path(CACHE_LOGS_DIR, false);
		}
		if (TWFunc::Path_Exists(se_context_check)) {
			ret = lgetfilecon(se_context_check.c_str(), &contexts);
			if (ret < 0) {
				LOGINFO("Could not check %s SELinux contexts, using /system/bin/teamwin instead which may be inaccurate.\n", se_context_check.c_str());
				lgetfilecon("/system/bin/teamwin", &contexts);
			}
		}
		if (ret < 0) {
			gui_warn("no_kernel_selinux=Kernel does not have support for reading SELinux contexts.");
		} else {
			free(contexts);
			gui_msg("full_selinux=Full SELinux support is present.");
		}
	}
}

void TWFunc::List_Mounts() {
	std::vector<std::string> mounts;
	read_file("/proc/mounts", mounts);
	LOGINFO("Mounts:\n");
	for (auto&& mount: mounts) {
		LOGINFO("%s\n", mount.c_str());
	}
}

bool TWFunc::Get_Encryption_Policy(fscrypt_encryption_policy &policy, std::string path) {
	if (!TWFunc::Path_Exists(path)) {
		LOGERR("Unable to find %s to get policy\n", path.c_str());
		return false;
	}
	if (!fscrypt_policy_get_struct(path.c_str(), &policy)) {
		LOGERR("No policy set for path %s\n", path.c_str());
		return false;
	}
	return true;
}

bool TWFunc::Set_Encryption_Policy(std::string path, const fscrypt_encryption_policy &policy) {
	if (!TWFunc::Path_Exists(path)) {
		LOGERR("unable to find %s to set policy\n", path.c_str());
		return false;
	}
	uint8_t binary_policy[FS_KEY_DESCRIPTOR_SIZE];
	char policy_hex[FS_KEY_DESCRIPTOR_SIZE_HEX];
	policy_to_hex(binary_policy, policy_hex);
	if (!fscrypt_policy_set_struct(path.c_str(), &policy)) {
		LOGERR("unable to set policy for path: %s\n", path.c_str());
		return false;
	}
	return true;
}

#endif // ndef BUILD_TWRPTAR_MAIN

void TWFunc::Deactivation_Process(void)
{
  if (TWFunc::To_Skip_OrangeFox_Process())
     {
	LOGINFO("\nOrangeFox: Skipping the OrangeFox Process.\n");
	New_Fox_Installation = 0;
	Fox_Force_Deactivate_Process = 0;
	DataManager::SetValue(FOX_FORCE_DEACTIVATE_PROCESS, 0);
	return;
     }

  // don't call this on first boot following fresh installation
  if (New_Fox_Installation != 1)
     {
         PrepareToFinish();
     }
   
  // advanced stock replace
  Disable_Stock_Recovery_Replace();

// patch ROM's fstab
#ifndef OF_USE_MAGISKBOOT
  bool patched_verity = false;
  bool patched_crypt = false;
  if ((DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1) || (Fox_Force_Deactivate_Process == 1))
     {
         patched_crypt = Patch_Forced_Encryption_In_System_Fstab();
     }
  
  if ((DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1) || (Fox_Force_Deactivate_Process == 1))
     {
	patched_verity = Patch_DM_Verity_In_System_Fstab();
     }
#endif
// patch ROM's fstab
  
// Should we skip the boot image patches?
  if (DontPatchBootImage() == true)
     {
     	if (New_Fox_Installation == 1 || MIUI_Is_Running())
     	   {
	      if (MIUI_Is_Running())
	         {
	            if (JustInstalledMiui())
	              {
	            #ifdef OF_NO_MIUI_PATCH_WARNING
	              	LOGINFO("OrangeFox: Not patching boot image. The ROM might not boot.\n");
	            	LOGINFO("NOTE: It is possible that MIUI will replace OrangeFox with the stock MIUI recovery.\n");
	            #else
	              	gui_print_color("warning", 
	              	"\nOrangeFox: WARNING! Not patching boot image.The ROM might not boot.\n");
	            	
	            	gui_print_color("warning", 
	            	"\nNOTE: It is possible that booting MIUI now will replace OrangeFox with the stock MIUI recovery.\n");
	            #endif
	              }
	         }
	   }
	LOGINFO("OrangeFox: skipping patching of boot image on device: %s\n", Fox_Current_Device.c_str());
	New_Fox_Installation = 0;
        Fox_Force_Deactivate_Process = 0;
        DataManager::SetValue(FOX_FORCE_DEACTIVATE_PROCESS, 0);	
        return;
     }   
// end
  
  gui_msg(Msg(msg::kProcess, "of_run_process=Starting '{1}' process")
      ("OrangeFox"));

// --- new method, using magiskboot plus a modded script
#ifdef OF_USE_MAGISKBOOT
  int res = TWFunc::Patch_DMVerity_ForcedEncryption_Magisk();
  if (res != 0)
     {
	gui_msg(Msg(msg::kProcess, "of_run_process_fail=Unable to finish '{1}' process") ("OrangeFox"));
     }
  else
     {
    	gui_msg(Msg(msg::kProcess, "of_run_process_done=Finished '{1}' process") ("OrangeFox"));
     }
#else // OF_USE_MAGISKBOOT
  if (!Unpack_Image("/boot"))
     {
	LOGERR("Deactivation_Process: Unable to unpack boot image\n");
	return;
     }

  // dm-verity #2
  if ((DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1) || (Fox_Force_Deactivate_Process == 1))
     {
	  patched_verity = Patch_DM_Verity();
	  if (patched_verity)
	  {
	      gui_msg("of_dm_verity=Successfully patched DM-Verity");
	  }
	  else
	  {
	     gui_msg("of_dm_verity_off=DM-Verity is not enabled");
	  }
     }

  // forced encryption #2
  if ((DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1) || (Fox_Force_Deactivate_Process == 1))
     {
	  patched_crypt = Patch_Forced_Encryption();
	  if (patched_crypt)
	     {
	        gui_msg("of_encryption=Successfully patched forced encryption");
	     }
	  else
	     {
	        gui_msg("of_encryption_off=Forced Encryption is not enabled");
	     }
     }

  if (!Repack_Image("/boot"))
     {
	gui_msg(Msg
	  (msg::kProcess, "of_run_process_fail=Unable to finish '{1}' process")
	  ("OrangeFox"));
     }
  else
     {
       gui_msg(Msg(msg::kProcess, "of_run_process_done=Finished '{1}' process")
	  ("OrangeFox"));
     }
#endif // OF_USE_MAGISKBOOT
  Fox_Force_Deactivate_Process = 0;
  DataManager::SetValue(FOX_FORCE_DEACTIVATE_PROCESS, 0);
}

void TWFunc::Patch_AVB20(bool silent)
{
#if defined(OF_PATCH_AVB20) && !defined(OF_SKIP_ORANGEFOX_PROCESS) && !defined(OF_VANILLA_BUILD)
std::string zipname = FFiles_dir + "/OF_avb20/OF_avb20.zip";
int res=0, wipe_cache=0;
std::string magiskboot = TWFunc::Get_MagiskBoot();
  if (!TWFunc::Path_Exists(magiskboot))
     {
        gui_print("ERROR - cannot find magiskboot\n");
  	return;
     }

   if (!TWFunc::Path_Exists(zipname))
     {
        gui_print("ERROR - cannot find %s\n", zipname.c_str());
  	return;
     }

   DataManager::SetValue(FOX_INSTALL_PREBUILT_ZIP, "1");
 
   if (silent)
     setenv("AVB_REPORT_PROGRESS", "0", 1);
   else
     setenv("AVB_REPORT_PROGRESS", "1", 1);
   usleep(4096);
   res = TWinstall_zip(zipname.c_str(), &wipe_cache);
   usleep(4096);
   setenv("AVB_REPORT_PROGRESS", "", 1);
   DataManager::SetValue(FOX_INSTALL_PREBUILT_ZIP, "0");
#endif
}

int TWFunc::Patch_DMVerity_ForcedEncryption_Magisk(void)
{
std::string keepdmverity, keepforcedencryption;
std::string zipname = FFiles_dir + "/OF_verity_crypt/OF_verity_crypt.zip";
int res=0, wipe_cache=0;
std::string magiskboot = TWFunc::Get_MagiskBoot();

  if (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) != 1 
   && DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) != 1)
     return 0;

  if (!TWFunc::Path_Exists(magiskboot))
     {
        gui_print("ERROR - cannot find magiskboot\n");
  	return 1;
     }

   if (!TWFunc::Path_Exists(zipname))
     {
        gui_print("ERROR - cannot find %s\n", zipname.c_str());
  	return 1;
     }
     /*
    if (DataManager::GetIntValue(FOX_DISABLE_DM_VERITY) == 1)
	keepdmverity = "false";
    else */
	keepdmverity = "true";

    if (DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION) == 1)
	{
	#ifdef OF_DONT_PATCH_ENCRYPTED_DEVICE
	   if (StorageIsEncrypted())
	         keepforcedencryption = "true";
	   else
	#endif
	   keepforcedencryption = "false";
	}
   else	
	keepforcedencryption = "true";

   setenv("KEEP_VERITY", keepdmverity.c_str(), 1);
   setenv("KEEP_FORCEENCRYPT", keepforcedencryption.c_str(), 1);
   DataManager::SetValue(FOX_INSTALL_PREBUILT_ZIP, "1");

   // see whether we have just installed a MIUI ROM or a custom ROM
   if (TWFunc::JustInstalledMiui())
      TWFunc::Fox_Property_Set("orangefox.miui.rom", "1");
    else
      TWFunc::Fox_Property_Set("orangefox.miui.rom", "0"); // if we have just installed a custom ROM, don't abandon the DFE process

   usleep(4096);

   res = TWinstall_zip(zipname.c_str(), &wipe_cache);

   setenv ("KEEP_VERITY", "", 1);
   setenv ("KEEP_FORCEENCRYPT", "", 1);
   DataManager::SetValue(FOX_INSTALL_PREBUILT_ZIP, "0");

   return res;
}

void TWFunc::Run_Pre_Flash_Protocol(bool forceit)
{
#ifdef OF_SUPPORT_PRE_FLASH_SCRIPT
  // don't run this for ROMs (unless forced)
  if ((forceit == true) || (DataManager::GetIntValue(FOX_ZIP_INSTALLER_CODE) == 0))
    {
      // don't run this for built-in zips
      if ((forceit == true) || (DataManager::GetIntValue(FOX_INSTALL_PREBUILT_ZIP) != 1))
        {
          string pre_runner = "/sbin/fox_pre_flash";
          if (TWFunc::Path_Exists(pre_runner))
	   {
	      TWFunc::Exec_Cmd(pre_runner.c_str());
	      DataManager::SetValue("FOX_PRE_FLASH_SCRIPT", "2");
	   }
        }
    }
#endif
}

void TWFunc::Run_Post_Flash_Protocol(void)
{
#ifdef OF_SUPPORT_PRE_FLASH_SCRIPT
    string pre_runner = "/sbin/fox_pre_flash";
    if (TWFunc::Path_Exists(pre_runner))
      {
         if (DataManager::GetIntValue("FOX_PRE_FLASH_SCRIPT") == 2)
            {
	       pre_runner = "/sbin/fox_pre_flash --unmount";
	       TWFunc::Exec_Cmd(pre_runner.c_str());
	       DataManager::SetValue("FOX_PRE_FLASH_SCRIPT", "1");
	    }
      }
#endif
}

bool TWFunc::MIUI_Is_Running(void)
{
   if (Fox_Current_ROM_IsMIUI == 1 || TWFunc::JustInstalledMiui() || TWFunc::Fox_Property_Get("orangefox.miui.rom") == "1")
      return true;
   else
      return false; 
}

bool TWFunc::Has_System_Root(void)
{
 string info = TWFunc::System_Property_Get("ro.build.system_root_image");
 return (info == "true");
}

int TWFunc::Rename_File(std::string oldname, std::string newname)
{
   return rename(oldname.c_str(), newname.c_str());
}

int TWFunc::Get_Android_SDK_Version(void)
{
int sdkver = 21;
string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
 if (!sdkverstr.empty())
   {
      sdkver = atoi(sdkverstr.c_str());
   }
 return sdkver;
}

string TWFunc::Get_MagiskBoot(void)
{
string s = Fox_Bin_Dir + "/magiskboot";
  if (TWFunc::Path_Exists(s))
     return s;
  s = "/sbin/magiskboot";
  if (TWFunc::Path_Exists(s))
     return s;
  else
     return "magiskboot";
}

// hopefully, this function will be obsolete one day ... //
void TWFunc::Setup_Verity_Forced_Encryption(void)
{
  DataManager::SetValue(FOX_ADVANCED_STOCK_REPLACE, "1");

/* untick the dm-verity and/or forced-encryption boxes on every bootup 
(regardless of user settings) on devices that have problems with disabling them */
#ifdef OF_KEEP_DM_VERITY_FORCED_ENCRYPTION
  DataManager::SetValue(FOX_DISABLE_DM_VERITY, "0");
  DataManager::SetValue(FOX_DISABLE_FORCED_ENCRYPTION, "0");
#endif

#ifdef OF_KEEP_FORCED_ENCRYPTION
  DataManager::SetValue(FOX_DISABLE_FORCED_ENCRYPTION, "0");
#endif

#ifdef OF_KEEP_DM_VERITY
  DataManager::SetValue(FOX_DISABLE_DM_VERITY, "0");
#endif

/* tick the disable dm-verity and/or forced-encryption boxes on every bootup 
(regardless of user settings) on devices that require them */
#ifdef OF_FORCE_DISABLE_DM_VERITY_FORCED_ENCRYPTION
  DataManager::SetValue(FOX_DISABLE_DM_VERITY, "1");
  DataManager::SetValue(FOX_DISABLE_FORCED_ENCRYPTION, "1");
#endif

#ifdef OF_FORCE_DISABLE_FORCED_ENCRYPTION
  DataManager::SetValue(FOX_DISABLE_FORCED_ENCRYPTION, "1");
#endif

#ifdef OF_FORCE_DISABLE_DM_VERITY
  DataManager::SetValue(FOX_DISABLE_DM_VERITY, "1");
#endif

#ifdef OF_VANILLA_BUILD
  DataManager::SetValue(FOX_DISABLE_DM_VERITY, "0");
  DataManager::SetValue(FOX_DISABLE_FORCED_ENCRYPTION, "0");
  DataManager::SetValue(FOX_ADVANCED_STOCK_REPLACE, "0");
#endif
}

void TWFunc::Dump_Current_Settings(void)
{
   LOGINFO("**********************************************************\n");
   LOGINFO("DEBUG - OrangeFox: settings being used:\n");   
   LOGINFO("- Disable DM-Verity=%i\n", DataManager::GetIntValue(FOX_DISABLE_DM_VERITY));
   LOGINFO("- Disable Forced Encryption=%i\n", DataManager::GetIntValue(FOX_DISABLE_FORCED_ENCRYPTION));
   LOGINFO("- Aggressive stock recovery deactivation=%i\n", DataManager::GetIntValue(FOX_ADVANCED_STOCK_REPLACE));

   #ifdef OF_DONT_PATCH_ON_FRESH_INSTALLATION
   LOGINFO("- Don't patch on fresh ROM installation=1\n");
   #else
   LOGINFO("- Don't patch on fresh ROM installation=0\n");
   #endif

   #ifdef OF_DONT_PATCH_ENCRYPTED_DEVICE
   LOGINFO("- Don't patch encrypted devices=1\n");
   #else
   LOGINFO("- Don't patch encrypted devices=0\n");
   #endif

   #ifdef OF_USE_MAGISKBOOT
   LOGINFO("- Use magiskboot=1\n");
   #else
   LOGINFO("- Use magiskboot=0\n");
   #endif

   #ifdef OF_USE_MAGISKBOOT_FOR_ALL_PATCHES
   LOGINFO("- Use magiskboot for all patches=1\n");
   #else
   LOGINFO("- Use magiskboot for all patches=0\n");
   #endif

   #ifdef OF_FORCE_MAGISKBOOT_BOOT_PATCH_MIUI
   LOGINFO("- Force magiskboot patch on MIUI ROM installation=1\n");
   #else
   LOGINFO("- Force magiskboot patch on MIUI ROM installation=0\n");
   #endif

   LOGINFO("**********************************************************\n");
}

void TWFunc::Reset_Clock(void)
{
   string fox_build_date_utc = TWFunc::File_Property_Get ("/etc/fox.cfg", "ro.build.date.utc_fox");
   if (!fox_build_date_utc.empty())
      {
        TWFunc::Exec_With_Output("date -s \"@" + fox_build_date_utc + "\" > /dev/null");
      }
}

bool TWFunc::Check_OrangeFox_Overwrite_FromROM(bool WarnUser, const std::string name)
{
#ifndef OF_CHECK_OVERWRITE_ATTEMPTS
   return false;
#else
  // turn on debug screen (for OTA)
  DataManager::SetValue("ota_new_screen", "1");
  
  // proceed
  if (WarnUser)
    {
      int i;
      int j = 40;
      gui_print_color("error", "\nALERT!\nThis ROM (%s) wants to overwrite your recovery partition!!!\n\n", name.c_str());
      gui_print_color("error", "I will pause for %i seconds.\n\nTo stop this installation, hard-reboot the device now!\n\n", j);
      gui_print("The %i-second countdown will start in 20 seconds ...\n", j);
      sleep(20);
      for (i = j; i > 0; i--)
         {
            gui_print("Reboot now! %i seconds left!\n", i);
            sleep(1);
         }
      gui_print_color("error", "\n\nSo, you have chosen to continue with \"%s\"! That seems *very* trusting! Good luck!\n\n", name.c_str());
      return true;
    }
  else
    {
      if (DataManager::GetIntValue("found_fox_overwriting_rom") == 1)
      {
      	gui_print_color("error",
      	"\nALERT!\nThis ROM (%s) may now have overwritten your recovery partition!\n\nGood luck!\n\n", name.c_str());
      }
      DataManager::SetValue("found_fox_overwriting_rom", "0");
      return true;
    }
#endif
}

void TWFunc::AppendLineToFile(string file_path, string line)
{
    std::ofstream file;
    file.open(file_path, std::ios::out | std::ios::app);
    file << line << std::endl;
}

void TWFunc::CreateNewFile(string file_path)
{
  string blank = "";
  string bak = file_path;
  if (TWFunc::Path_Exists(bak))
    unlink(file_path.c_str());
  ofstream file;
  file.open(file_path.c_str());
  file << blank;
  file.close();
  chmod (file_path.c_str(), 0644);
}

bool TWFunc::To_Skip_OrangeFox_Process(void)
{
  #if defined(OF_SKIP_ORANGEFOX_PROCESS) || defined(OF_VANILLA_BUILD)
     return true;
  #else
     return false;
  #endif
}

string TWFunc::ConvertTime(time_t time)
{
  char buff[32];
  strftime(buff, 32, "%y/%m/%d %H:%M", localtime(&time));
  return buff;
}

void TWFunc::UseSystemFingerprint(void)
{
string rom_finger_print = "";
string tmp = "\"";

  rom_finger_print = TWFunc::Fox_Property_Get("orangefox.system.fingerprint");

  if (rom_finger_print.empty()) {
     if (TWFunc::Path_Exists(orangefox_cfg))
       {
	rom_finger_print = File_Property_Get(orangefox_cfg, "ROM_FINGERPRINT");
       }
   }

  if (rom_finger_print.empty())
 	rom_finger_print = System_Property_Get("ro.system.build.fingerprint");

  if (rom_finger_print.empty())
  	rom_finger_print = System_Property_Get("ro.build.fingerprint");

  if (!rom_finger_print.empty())
     {
  	LOGINFO("- Using the ROM's fingerprint (%s)\n", rom_finger_print.c_str());
  	TWFunc::Fox_Property_Set("ro.build.fingerprint", tmp + rom_finger_print + tmp);
     }
  else LOGINFO("- ROM fingerprint not available\n");
}

string TWFunc::get_assert_device(const string filename)
{
string str = "";
string temp = find_phrase(filename, "ro.product.device");

   if (temp.empty())
	temp = find_phrase(filename, "ro.build.product");

   if (temp.empty())
      return str;
   
   // either assert or getprop should be on the ro.product.device line
   if (temp.find("assert") == std::string::npos && temp.find("getprop") == std::string::npos)
      return str;

   // we are also looking for E3004 and abort on the same line
   if (temp.find("E3004") != std::string::npos && temp.find("abort") != std::string::npos)
      {
        //gui_print("- Found E3004 and abort on the ro.product.device line !\n");
      }
   else   
      {
        //gui_print("- E3004 and abort not found on the ro.product.device line! Search again ...\n");
        string temp2 = find_phrase(filename, "E3004");
   	if (temp2.empty()) // we really need this error code
   	   return str;
   	   
   	if (temp2.find("abort") == std::string::npos) // we also need the abort statement
           return str;
        //gui_print("- Finally found E3004 and abort!\n");
      }

   // parse the string to extract the device name
   str = DeleteBefore(temp, "==", true);// remove everything before "=="
   str = DeleteAfter(str, "||"); 	// remove everything after "||"
   str = removechar(str, '"');   	// remove quotation marks
   str = removechar(str, ' ');		// remove spaces

   return str;
}

string TWFunc::removechar(const string src, const char chars)
{
std::string str = src;
int i = str.find(chars);
   while (i != (int)std::string::npos)
   {
     str.erase(i, 1);
     i = str.find(chars);
   }
   return str;
}

string TWFunc::lowercase (const string src)
{
   string str = src;
   transform(str.begin(), str.end(), str.begin(), ::tolower);
   return str;
}

string TWFunc::uppercase (const string src)
{
   string str = src;
   transform(str.begin(), str.end(), str.begin(), ::toupper);
   return str;
}

/* find the position of "subs" in "str" (or -1 if not found) */
int TWFunc::pos (const string subs, const string str)
{
  return str.find(subs);
}

string TWFunc::ltrim(std::string str, const std::string chars)
{
    str.erase(0, str.find_first_not_of(chars));
    return str;
}
 
string TWFunc::rtrim(std::string str, const std::string chars)
{
    str.erase(str.find_last_not_of(chars) + 1);
    return str;
}

string TWFunc::trim(std::string str, const std::string chars)
{
    return ltrim(rtrim(str, chars), chars);
}

int TWFunc::DeleteFromIndex(std::string &Str, int Index, int Size)
{
  int len = Str.length();
  if (Index < 0 || Index > len || Size < 1)
     return -1;
  int i = (Size - Index);
  if (i >= len) 
     Size = i;
  Str.erase (Index, Size);
  return Size;
}

string TWFunc::DeleteBefore(const std::string Str, const std::string marker, bool removemarker)
{
  std::string src = Str;
  int i = src.find(marker);
  if (i == (int)std::string::npos) 
     return Str;
  if (removemarker) 
     i += marker.length();
  src.erase (0, i);
  return src;
}

string TWFunc::DeleteAfter(const std::string Str, const std::string marker)
{
  std::string src = Str;
  int i = src.find(marker);
  if (i == (int)std::string::npos) 
     return Str;
  src.erase (i, src.length());
  return src;
}

string TWFunc::find_phrase(std::string filename, std::string search)
{
  std::string line = "";
  std::string str = "";
  std::ifstream File;
  File.open(filename);
  if (File.is_open())
    {
      while (!File.eof())
	{
	  std::getline(File, line);
	  if (line.find(search) != std::string::npos)
	    {
	      File.close();
	      return line;
	    }
	}
      File.close();
    }
  return str;
}

std::string TWFunc::Remove_Beginning_Slash(const std::string& path) {
	std::string res;
	size_t pos = path.find_first_of("/");
	if (pos != std::string::npos) {
		res = path.substr(pos+1);
	}
	return res;
}

string TWFunc::Fox_Property_Get(string Prop_Name) {
	return android::base::GetProperty(Prop_Name, "");
}

bool TWFunc::Fox_Property_Set(const std::string Prop_Name, const std::string Value) {
  usleep(2048);
  bool res = android::base::SetProperty(Prop_Name, Value);
  if (!res && Fox_Property_Get(Prop_Name) != Value) {
    	usleep(1028);
	string tmp = "\"";
	string cmd = "/sbin/resetprop";

  	if (!Path_Exists(cmd))
    		cmd = Fox_Bin_Dir + "/resetprop";

  	if (!Path_Exists(cmd))
    		cmd = Fox_Bin_Dir + "/setprop";

    	if (Path_Exists(cmd)) {
  	    int ret = Exec_Cmd(cmd + " " + Prop_Name + " " + tmp + Value + tmp);
  	    //gui_print("DEBUG rerun TWFunc::Fox_Property_Set() - return value of property_set of (%s => %s)=%i\n", Prop_Name.c_str(), Value.c_str(), ret);
  	    res = (ret == 0);
    	}
  }
  usleep(2048);
  return res;
}

bool TWFunc::Has_Dynamic_Partitions(void) {
	return (Fox_Property_Get("ro.boot.dynamic_partitions") == "true");
}

void TWFunc::Mapper_to_BootDevice(void) {
    	#if defined(AB_OTA_UPDATER) || defined(OF_AB_DEVICE)
    	return;
    	#endif

	if (Has_Dynamic_Partitions()) {
 		#ifdef BOARD_SUPER_PARTITION_PARTITION_LIST
 		string list = BOARD_SUPER_PARTITION_PARTITION_LIST;
 		string delim = ",";
  		std::vector <std::string> partitions = TWFunc::Split_String(list, delim);
 		printf("=> Linking dynamic partitions (%s)\n", list.c_str());
  		string src, dest, dest2;
		for (auto&& part : partitions) {
		        part = lowercase(trim(part));
   		   	src = "/dev/block/mapper/" + part;
   		   	dest = "/dev/block/by-name/" + part;
   		   	dest2 = "/dev/block/bootdevice/by-name/" + part;
 			LOGINFO("=> Symlink %s => %s\n", src.c_str(), dest.c_str());
		   	symlink(src.c_str(), dest.c_str());
 			LOGINFO("=> Symlink %s => %s\n", src.c_str(), dest2.c_str());
		   	symlink(src.c_str(), dest2.c_str());
		}
		#endif
	}
}

void TWFunc::PostWipeEncryption(void) {
#ifdef OF_RUN_POST_FORMAT_PROCESS
  gui_print("I: Recreating /data/media/0...\n");
  sleep(1);
  TWFunc::Recursive_Mkdir("/data/media/0", false);
  TWFunc::Recursive_Mkdir("/data/media/0/Fox/logs", false);

  gui_print("I: Copying recovery.log...\n");
  TWFunc::copy_file("/tmp/recovery.log", "/data/media/0/Fox/logs/lastrecoverylog.log", 0644);
  sleep(1);

  gui_print("I: Binding the internal storage...\n");
  string cmd = "/system/bin/mount";
  if (!TWFunc::Path_Exists(cmd))
     cmd = "/sbin/mount";
  cmd = cmd + " -o bind /data/media/0 /sdcard";
  TWFunc::Exec_Cmd(cmd);
  sleep(1);
  sync();
  gui_msg("done=Done.");
#endif
}
//
