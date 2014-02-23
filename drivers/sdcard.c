#include "sdcard.h"
#include "chprintf.h"
#include <stdio.h>
#include <string.h>
#include "drivers/debug.h"

FATFS MMC_FS;
MMCDriver MMCD1;
static bool_t fs_ready = FALSE;
static SPIConfig hs_spicfg = { NULL, GPIOB, 15, 0 };
static SPIConfig ls_spicfg = { NULL, GPIOB, 15, SPI_CR1_BR_2 | SPI_CR1_BR_1 };
static MMCConfig mmc_cfg = { &SPID1, &ls_spicfg, &hs_spicfg };

// where these go ? mmc_is_protected, mmc_is_inserted


void sdcard_enable(void)
{
    D_ENTER();
    power_request(SDCARD_POWER_DOMAIN);
    D_EXIT();
}

void sdcard_disable(void)
{
    D_ENTER();
    if (sdcard_fs_ready())
    {
        sdcard_unmount();
    }
    power_release(SDCARD_POWER_DOMAIN);
    D_EXIT();
}

bool_t sdcard_fs_ready(void)
{
    return fs_ready;
}

FRESULT sdcard_unmount(void)
{
    D_ENTER();
    FRESULT err;
    err = f_mount(0, NULL);
    mmcDisconnect(&MMCD1);
    fs_ready = FALSE;
    D_EXIT();
    return err;
}

static WORKING_AREA(mount_thread_WA, 4096);
static msg_t mount_thread(void *arg)
{
    (void)arg;
    D_ENTER();
    sdcard_mount();
    D_EXIT();
    return -1;
}

static void create_mount_thread()
{
    D_ENTER();
    chThdCreateStatic(mount_thread_WA, sizeof(mount_thread_WA), NORMALPRIO, (tfunc_t)mount_thread, NULL);
    D_EXIT();
}

FRESULT sdcard_mount(void)
{
    D_ENTER();
    FRESULT err;
    if(mmcConnect(&MMCD1))
    {
        // TODO: how to access the shell output stream ?
        _DEBUG("SD: Failed to connect to card\r\n");
        D_EXIT();
        return FR_NOT_READY;
    }
    else
    {
        _DEBUG("SD: Connected to card\r\n");
    }
    
    err = f_mount(0, &MMC_FS);
    if(err != FR_OK)
    {
        _DEBUG("SD: f_mount() failed %d\r\n", err);
        mmcDisconnect(&MMCD1);
        D_EXIT();
        return err;
    }
    else
    {
        _DEBUG("SD: File system mounted\r\n");
    }
    fs_ready = TRUE;
    D_EXIT();
    return err;
}

void sdcard_insert_handler(eventid_t id)
{
    (void)id;
    D_ENTER();
    /**
     * Actually we cannot call this from interrupt/lock zone, raise flag or something
    sdcard_mount();
     */
    D_EXIT();
}

void sdcard_remove_handler(eventid_t id)
{
    (void)id;
    // TODO: Do we need to do something else to unmount the fs ?? (or is it too late)
    /**
     * Actually we cannot call this from interrupt/lock zone, raise flag or something
    sdcard_unmount();
     */
}

void sdcard_mmcd_init(void)
{
    D_ENTER();
    mmcObjectInit(&MMCD1);
    mmcStart(&MMCD1, &mmc_cfg);
    D_EXIT();
}

void sdcard_cmd_mount(BaseSequentialStream *chp, int argc, char *argv[])
{
    (void)argv;
    (void)argc;
    D_ENTER();
    if (sdcard_fs_ready())
    {
        chprintf(chp, "Already mounted\r\n");
        D_EXIT();
        return;
    }
    chprintf(chp, "Mounting filesystem\r\n");
    sdcard_enable();
    // Wait for the regulator to stabilize
    chThdSleepMilliseconds(100);
    create_mount_thread();
    chThdSleepMilliseconds(100);
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Mount failed\r\n");
        D_EXIT();
        return;
    }
    chprintf(chp, "Card mounted\r\n");
    D_EXIT();
}

void sdcard_cmd_unmount(BaseSequentialStream *chp, int argc, char *argv[])
{
    (void)argv;
    (void)argc;
    D_ENTER();
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Already unmounted\r\n");
        D_EXIT();
        return;
    }
    chprintf(chp, "Unmounting filesystem\r\n");
    sdcard_unmount();
    chThdSleepMilliseconds(100);
    sdcard_disable();
    chprintf(chp, "Done\r\n");
    D_EXIT();
}

FRESULT sdcard_scan_files(BaseSequentialStream *chp, char *path)
{
    D_ENTER();
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

    D_EXIT();
    return res;
}

void sdcard_cmd_ls(BaseSequentialStream *chp, int argc, char *argv[])
{
    D_ENTER();
    if (!sdcard_fs_ready())
    {
        chprintf(chp, "Not mounted\r\n");
        D_EXIT();
        return;
    }
    if (argc < 1)
    {
        sdcard_scan_files(chp, "/");
    }
    else
    {
        sdcard_scan_files(chp, argv[0]);
    }
    D_EXIT();
}

