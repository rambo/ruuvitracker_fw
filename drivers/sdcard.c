#include "sdcard.h"
FATFS MMC_FS;
MMCDriver MMCD1;
static bool_t fs_ready = FALSE;
static SPIConfig hs_spicfg = { NULL, GPIOB, 15, 0 };
static SPIConfig ls_spicfg = { NULL, GPIOB, 15, SPI_CR1_BR_2 | SPI_CR1_BR_1 };
static MMCConfig mmc_cfg = { &SPID1, &ls_spicfg, &hs_spicfg };

// where these go ? mmc_is_protected, mmc_is_inserted


void sdcard_enable(void)
{
    power_request(SDCARD_POWER_DOMAIN);
}

void sdcard_disable(void)
{
    if (sdcard_fs_ready())
    {
        sdcard_unmount();
    }
    power_release(SDCARD_POWER_DOMAIN);
}

bool_t sdcard_fs_ready(void)
{
    return fs_ready;
}

FRESULT sdcard_unmount(void)
{
    FRESULT err;
    err = f_mount(0, NULL);
    mmcDisconnect(&MMCD1);
    fs_ready = FALSE;
    return err;
}

FRESULT sdcard_mount(void)
{
    FRESULT err;
    if(mmcConnect(&MMCD1))
    {
        // TODO: how to access the shell output stream ?
        //chprintf((BaseSequentialStream *)&SDU2, "SD: Failed to connect to card\r\n");
        return FR_NOT_READY;
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
        return err;
    }
    else
    {
        //chprintf((BaseSequentialStream *)&SDU2, "SD: File system mounted\r\n");
    }
    fs_ready = TRUE;
    return err;
}

void sdcard_insert_handler(eventid_t id)
{
    (void)id;
    sdcard_mount();
}

void sdcard_remove_handler(eventid_t id)
{
    (void)id;
    // TODO: Do we need to do something else to unmount the fs ?? (or is it too late)
    sdcard_unmount();
}

void sdcard_mmcd_init(void)
{
    mmcObjectInit(&MMCD1);
    mmcStart(&MMCD1, &mmc_cfg);
}


