#include "sdcard.h"
#include "chprintf.h"
#include <stdio.h>
#include <string.h>

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

void sdcard_cmd_mount(BaseSequentialStream *chp, int argc, char *argv[])
{
    (void)argv;
    (void)argc;
    if (sdcard_fs_ready())
    {
        chprintf(chp, "Already mounted\r\n");
        return;
    }
    chprintf(chp, "Mounting filesystem\r\n");
    sdcard_enable();
    // Wait for the regulator to stabilize
    chThdSleepMilliseconds(100);
    sdcard_mount();
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Mount failed\r\n");
        return;
    }
    chprintf(chp, "Card mounted\r\n");
}

void sdcard_cmd_unmount(BaseSequentialStream *chp, int argc, char *argv[])
{
    (void)argv;
    (void)argc;
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Already unmounted\r\n");
        return;
    }
    chprintf(chp, "Unmounting filesystem\r\n");
    sdcard_unmount();
    chThdSleepMilliseconds(100);
    sdcard_disable();
    chprintf(chp, "Done\r\n");
}

FRESULT sdcard_scan_files(BaseSequentialStream *chp, char *path)
{
    FRESULT res;
    FILINFO fno;
    DIR dir;
    int i;
    char *fn;   /* This function is assuming non-Unicode cfg. */
#if _USE_LFN
    static char lfn[_MAX_LFN + 1];
    fno.lfname = lfn;
    fno.lfsize = sizeof(lfn);
#endif


    res = f_opendir(&dir, path);                       /* Open the directory */
    if (res == FR_OK) {
        i = strlen(path);
        for (;;) {
            res = f_readdir(&dir, &fno);                   /* Read a directory item */
            if (res != FR_OK || fno.fname[0] == 0) break;  /* Break on error or end of dir */
            if (fno.fname[0] == '.') continue;             /* Ignore dot entry */
#if _USE_LFN
            fn = *fno.lfname ? fno.lfname : fno.fname;
#else
            fn = fno.fname;
#endif
            if (fno.fattrib & AM_DIR) {                    /* It is a directory */
                sprintf(&path[i], "/%s", fn);
                res = sdcard_scan_files(chp, path);
                if (res != FR_OK) break;
                path[i] = 0;
            } else {                                       /* It is a file. */
                chprintf(chp, "%s/%s\n", path, fn);
            }
        }
    }

    return res;
}

void sdcard_cmd_ls(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Not mounted\r\n");
        return;
    }
    if (argc < 1)
    {
        sdcard_scan_files(chp, "/");
        return;
    }
    sdcard_scan_files(chp, argv[0]);
}

