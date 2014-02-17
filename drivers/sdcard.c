#include "ch.h"
#include "hal.h"
#include "power.h"
#include "sdcard.h"
#include <ff.h>
#include "chprintf.h"

static bool_t fs_ready = FALSE;
// TODO: Doublecheck these settings
static SPIConfig hs_spicfg = { NULL, GPIOB, 15, 0 };
static SPIConfig ls_spicfg = { NULL, GPIOB, 15, SPI_CR1_BR_2 | SPI_CR1_BR_1 };
static MMCConfig mmc_cfg = { &SPID1, &ls_spicfg, &hs_spicfg };

// where these go ? mmc_is_protected, mmc_is_inserted


bool_t sdcard_fs_ready(void)
{
    return fs_ready;
}

void sdcard_insert_handler(eventid_t id)
{
    FRESULT err;
    (void) id;
    if(mmcConnect(&MMCD1))
    {
        // TODO: how to access the shell output stream ?
        //chprintf((BaseSequentialStream *)&SDU2, "SD: Failed to connect to card\r\n");
        return;
    }
    else
    {
        //chprintf((BaseSequentialStream *)&SDU2, "SD: Connected to card\r\n");
    }
    
    err = f_mount(0, &MMC_FS);
    if(err != FR_OK)
    {
        //chprintf((BaseSequentialStream *)&SDU2, "SD: f_mount() failed %d\r\n", err);
        mmcDisconnect(&MMCD1);
        return;
    }
    else
    {
        //chprintf((BaseSequentialStream *)&SDU2, "SD: File system mounted\r\n");
    }
    fs_ready = TRUE;
}

void sdcard_remove_handler(eventid_t id)
{
    (void)id;
    // TODO: Do we need to do something else to unmount the fs ?? (or is it too late)
    fs_ready = FALSE;
}

void sdcard_mmcd_init(void)
{
    mmcStart(&MMCD1, &mmc_cfg);
}


