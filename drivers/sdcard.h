#ifndef SDCARD_H
#define SDCARD_H
#include "power.h"
#include <ff.h>

FATFS MMC_FS;
MMCDriver MMCD1;

#define SDCARD_POWER_DOMAIN = LDO2

/**
 * powers up the sdcard 
 *
 * mounting the FS is handled by the MMC driver insertion callback
 *
 */
void sdcard_enable(void);

/**
 * powers down the sdcard (if nothing else is active in the same power domain)
 *
 * Unmounting filesystem is handled by the MMC driver callback
 *
 */
void sdcard_disable(void);

/**
 * Check whether we have a mounted fs
 */
bool_t sdcard_fs_ready(void);

/**
 * MMCD system init 
 *
 * this should be called from main()
 */
void sdcard_mmcd_init(void);


/**
 * These callbacks are called by the MMC driver
 *
 * They handle mounting/unmounting the filesystem
 */
void sdcard_insert_handler(eventid_t id);
void sdcard_remove_handler(eventid_t id);

#endif