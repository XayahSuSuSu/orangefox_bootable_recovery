#ifndef RECOVERY_TWINSTALL_H_
#define RECOVERY_TWINSTALL_H_

int TWinstall_zip(const char* path, int* wipe_cache, bool check_for_digest = false);
int TWinstall_Run_OTA_BAK (bool reportback); // run the MIUI OTA backup; set some values of reportback is true

#endif  // RECOVERY_TWINSTALL_H_
