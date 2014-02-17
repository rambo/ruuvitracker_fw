#ifndef SDCARD_H
#define SDCARD_H
#include "power.h"


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




#endif