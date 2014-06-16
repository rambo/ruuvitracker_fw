#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ch.h"
#include "hal.h"
#include "test.h"

#include "shell.h"
#include "chprintf.h"
#include "power.h"
#include "drivers/usb_serial.h"
#include "drivers/gps.h"
#include "drivers/gsm.h"
#include "drivers/http.h"
#include "drivers/sha1.h"
#include "drivers/debug.h"
#include "drivers/reset_button.h"
#include "drivers/rtchelpers.h"
#include "drivers/testplatform.h"


#define SHA1_STR_LEN    40
#define SHA1_LEN    20
#define BLOCKSIZE   64
#define IPAD 0x36
#define OPAD 0x5C

/**
 * Backup domain data
 */
#define BACKUP_CONFIG_VERSION (0xfeeddeed + 0) // Increment the + part each time the config struct changes
struct backup_domain_data_t
{
    char apn[50];
    char pin[10];
    char trackerid[50];
    char sharedsecret[50];
    unsigned int interval;
    uint32_t config_version;
}; struct backup_domain_data_t * const backup_domain_data = (struct backup_domain_data_t *)BKPSRAM_BASE;
static bool backup_domain_data_is_sane(void)
{
    return backup_domain_data->config_version == BACKUP_CONFIG_VERSION;
}


/* Helper to get hash from library */
static void get_hash(SHA1Context *sha, char *p)
{
    int i;
    for(i=0; i<5; i++) {
        *p++ = (sha->Message_Digest[i]>>24)&0xff;
        *p++ = (sha->Message_Digest[i]>>16)&0xff;
        *p++ = (sha->Message_Digest[i]>>8)&0xff;
        *p++ = (sha->Message_Digest[i]>>0)&0xff;
    }
}

static char *sha1_hmac(const char *secret, const char *msg)
{
    static char response[SHA1_STR_LEN];
    SHA1Context sha1,sha2;
    char key[BLOCKSIZE];
    char hash[SHA1_LEN];
    int i;
    size_t len;

    len = strlen(secret);

    //Fill with zeroes
    memset(key, 0, BLOCKSIZE);

    if (len > BLOCKSIZE) {
        //Too long key, shorten with hash
        SHA1Reset(&sha1);
        SHA1Input(&sha1, (const unsigned char *)secret, len);
        SHA1Result(&sha1);
        get_hash(&sha1, key);
    } else {
        memcpy(key, secret, len);
    }

    //XOR key with IPAD
    for (i=0; i<BLOCKSIZE; i++) {
        key[i] ^= IPAD;
    }

    //First SHA hash
    SHA1Reset(&sha1);
    SHA1Input(&sha1, (const unsigned char *)key, BLOCKSIZE);
    SHA1Input(&sha1, (const unsigned char *)msg, strlen(msg));
    SHA1Result(&sha1);
    get_hash(&sha1, hash);

    //XOR key with OPAD
    for (i=0; i<BLOCKSIZE; i++) {
        key[i] ^= IPAD ^ OPAD;
    }

    //Second hash
    SHA1Reset(&sha2);
    SHA1Input(&sha2, (const unsigned char *)key, BLOCKSIZE);
    SHA1Input(&sha2, (const unsigned char *)hash, SHA1_LEN);
    SHA1Result(&sha2);

    for(i = 0; i < 5 ; i++) {
        sprintf(response+i*8,"%.8x", sha2.Message_Digest[i]);
    }
    return response;
}

struct json_t {
     char *name;
     char *value;
};

#define ELEMS_IN_EVENT 6
#define MAC_INDEX 6
struct json_t js_elems[ELEMS_IN_EVENT + 1] = {
     /* Names must be in alphabethical order */
     { "latitude",      NULL },
     { "longitude",     NULL },
     { "session_code",  NULL },
     { "time",          NULL },
     { "tracker_code",  NULL },
     { "version",       "1" },
     /* Except "mac" that must be last element */
     { "mac",           NULL },
};

/* Replace elements value */
void js_replace(char *name, char *value)
{
     int i;
     for (i=0;i<ELEMS_IN_EVENT;i++) {
      if (0 == strcmp(name, js_elems[i].name)) {
           if (js_elems[i].value)
            free(js_elems[i].value);
           js_elems[i].value = value;
           break;
      }
     }
}

void calculate_mac(char *secret)
{
     static char str[4096]; //Assume that it fits
     int i;
     str[0] = 0;
     for (i=0;i<ELEMS_IN_EVENT;i++) {
      strcat(str, js_elems[i].name);
      strcat(str, ":");
      strcat(str, js_elems[i].value);
      strcat(str, "|");
     }
     js_elems[MAC_INDEX].value = sha1_hmac(secret, str);
     _DEBUG("Calculated MAC %s\r\nSTR: %s\r\n", js_elems[MAC_INDEX].value, str);
}

char *js_tostr(void)
{
     int i;
     static char str[4096]; //Again..assume.. when we crash, fix this.
     str[0] = '{';
     str[1] = 0;
     for (i=0;i<ELEMS_IN_EVENT+1;i++) {
      if (i)
           strcat(str, ",");
      strcat(str, "\"");
      strcat(str, js_elems[i].name);
      strcat(str, "\": \"");
      strcat(str, js_elems[i].value);
      strcat(str, "\" ");
     }
     strcat(str, "}");
     return str;
}

static void send_event(struct gps_data_t *gps)
{
     static char buf[255];
     HTTP_Response *response;
     char *json;
     static int first_time = 1;

     sprintf(buf, "%f", (float)gps->lat);
     js_replace("latitude", strdup(buf));
     sprintf(buf, "%f", (float)gps->lon);
     js_replace("longitude", strdup(buf));
     sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                     gps->dt.year, gps->dt.month, gps->dt.day, gps->dt.hh, gps->dt.mm, gps->dt.sec);
     js_replace("time", strdup(buf));
     js_replace("tracker_code", backup_domain_data->trackerid);
     if (first_time) {
      js_replace("session_code", strdup(buf));
      first_time = 0;
     }
     calculate_mac(backup_domain_data->sharedsecret);
     json = js_tostr();

     _DEBUG("Sending JSON event:\r\n%s\r\nlen = %d\r\n", json, strlen(json));
     gsm_gprs_enable();
     response = http_post("http://dev-server.ruuvitracker.fi/api/v1-dev/events", json, "application/json");
     gsm_gprs_disable();
     if (!response) {
      _DEBUG("HTTP POST failed\r\n");
     } else {
      _DEBUG("HTTP response %d:\r\n%s\r\n", response->code, response->content);
      http_free(response);
     }
}

static WORKING_AREA(myWA, 4096);
__attribute__((noreturn))
static void tracker_th(void *args)
{ 
    (void)args;
    struct gps_data_t gps;
    int gsmstate=0;
    int gsmreply=0;
    EventListener gpslistener;
    int fix_type;

     tp_sync(1);
     gsm_start();
     // Wait for PIN prompt if any
     while (gsmstate < 3) // STATE_ASK_PIN
     {
        gsmstate = gsm_get_state();
        chThdSleepMilliseconds(100);
     }
     if (gsmstate == 3) // STATE_ASK_PIN
     {
        // Try to enter the pin once
        gsmreply = gsm_cmd_fmt("AT+CPIN=%s", backup_domain_data->pin);
        if (gsmreply != AT_OK)
        {
            // TODO: Setup a global "config error" variable and mask the pin_error from there.
            _DEBUG("PIN unlock FAILED\r\n");
            // Light the red led and go to infinite loop
            palSetPad(GPIOB, GPIOB_LED2);
            while (TRUE)
            {
                chThdSleepMilliseconds(1000);
            }
        }
     }
     tp_sync(2);
     // Disable netlight
     gsm_uart_write("AT+CNETLIGHT=0\r\n");
     gsm_set_apn(backup_domain_data->apn);
     gps_start();
     // Wait for GPS to become ready for commands
     while(gps_get_serial_port_validated() < 1)
     {
        chThdSleepMilliseconds(100);
     }
     gps_set_update_interval(backup_domain_data->interval);
     tp_sync(3);

    /*
     * Register the event listener with the event source.  This is the only
     * event this thread will be waiting for, so we choose the lowest eid.
     * However, the eid can be anything from 0 - 31.
     */
    chEvtRegister(&gps_fix_updated, &gpslistener, 0);
    while (TRUE)
    {
        // Wait for fix...
        tp_sync(4);
        _DEBUG("Waiting for a fix\r\n");
        /*
         * We can now wait for our event.  Since we will only be waiting for
         * a single event, we should use chEvtWaitOne()
         */
        chEvtWaitOne(EVENT_MASK(0));
        fix_type = chEvtGetAndClearFlags(&gpslistener);
        _DEBUG("Fix change signal received, type=%d\r\n", fix_type);
        if (fix_type == GPS_FIX_TYPE_NONE)
        {
            // No fix
            continue;
        }

        gps = gps_get_data_nonblock();
        send_event(&gps);
        // Doublecheck that this is set
        gps_set_update_interval(backup_domain_data->interval);
        tp_sync(5);
    }
}


/**
 * Set the config variables
 */
static void cmd_interval(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        if (!backup_domain_data_is_sane())
        {
            chprintf(chp, "Backup data config version %x does not match expected %x\r\n", backup_domain_data->config_version, BACKUP_CONFIG_VERSION);
            return;
        }
        chprintf(chp, "GPS update interval is %d ms\r\n", backup_domain_data->interval);
    }
    else
    {
        backup_domain_data->interval=atoi(argv[0]);
        // Set the signature (maybe someday it's a checksum)
        backup_domain_data->config_version = BACKUP_CONFIG_VERSION;
        if (gps_get_serial_port_validated())
        {
            gps_set_update_interval(backup_domain_data->interval);
        }
    }
}

static void cmd_apn(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        if (!backup_domain_data_is_sane())
        {
            chprintf(chp, "Backup data config version %x does not match expected %x\r\n", backup_domain_data->config_version, BACKUP_CONFIG_VERSION);
            return;
        }
        chprintf(chp, "APN is '%s'\r\n", backup_domain_data->apn);
    }
    else
    {
        strncpy(backup_domain_data->apn, argv[0], sizeof(backup_domain_data->apn));
        // Set the signature (maybe someday it's a checksum)
        backup_domain_data->config_version = BACKUP_CONFIG_VERSION;
    }
}

static void cmd_pin(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        if (!backup_domain_data_is_sane())
        {
            chprintf(chp, "Backup data config version %x does not match expected %x\r\n", backup_domain_data->config_version, BACKUP_CONFIG_VERSION);
            return;
        }
        // TODO: Replace with a message saying reading back the pin is not allowed
        chprintf(chp, "PIN is '%s'\r\n", backup_domain_data->pin);
    }
    else
    {
        strncpy(backup_domain_data->pin, argv[0], sizeof(backup_domain_data->pin));
        // Set the signature (maybe someday it's a checksum)
        backup_domain_data->config_version = BACKUP_CONFIG_VERSION;
    }
}

static void cmd_trackerid(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        if (!backup_domain_data_is_sane())
        {
            chprintf(chp, "Backup data config version %x does not match expected %x\r\n", backup_domain_data->config_version, BACKUP_CONFIG_VERSION);
            return;
        }
        // TODO: Replace with a message saying reading back the pin is not allowed
        chprintf(chp, "Tracker-id is '%s'\r\n", backup_domain_data->trackerid);
    }
    else
    {
        strncpy(backup_domain_data->trackerid, argv[0], sizeof(backup_domain_data->trackerid));
        // Set the signature (maybe someday it's a checksum)
        backup_domain_data->config_version = BACKUP_CONFIG_VERSION;
    }
}

static void cmd_sharedsecret(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        if (!backup_domain_data_is_sane())
        {
            chprintf(chp, "Backup data config version %x does not match expected %x\r\n", backup_domain_data->config_version, BACKUP_CONFIG_VERSION);
            return;
        }
        // TODO: Replace with a message saying reading back the secret is not allowed
        chprintf(chp, "Shared secret is '%s'\r\n", backup_domain_data->sharedsecret);
    }
    else
    {
        strncpy(backup_domain_data->sharedsecret, argv[0], sizeof(backup_domain_data->sharedsecret));
        // Set the signature (maybe someday it's a checksum)
        backup_domain_data->config_version = BACKUP_CONFIG_VERSION;
    }
}

static void cmd_gsm(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        chprintf(chp, "Usage: gsm AT_COMMAND\r\n");
    }
    gsm_cmd(argv[0]);
}

static void cmd_gps(BaseSequentialStream *chp, int argc, char *argv[])
{
    if (argc < 1)
    {
        chprintf(chp, "Usage: gps AT_COMMAND\r\n");
    }
    gps_cmd(argv[0]);
}

#define SHELL_WA_SIZE   THD_WA_SIZE(2048)

static const ShellCommand commands[] = {
    {"apn", cmd_apn},
    {"pin", cmd_pin},
    {"trackerid", cmd_trackerid},
    {"secret", cmd_sharedsecret},
    {"interval", cmd_interval},
    {"gsm", cmd_gsm},
    {"gps", cmd_gps},
    {"alarm", cmd_alarm},
    {"date", cmd_date},
    {"wakeup", cmd_wakeup},
    {NULL, NULL}
};

static const ShellConfig shell_cfg1 = {
    (BaseSequentialStream *)&SDU,
    commands
};


int main(void)
{
    Thread *shelltp = NULL;

    halInit();
    chSysInit();
    tp_init();
    usb_serial_init();
    button_init();

    shellInit();

    // The tracker thread, this will initialized gps, gsm etc
    if (backup_domain_data_is_sane())
    {
        chThdCreateStatic(myWA, sizeof(myWA), NORMALPRIO, (tfunc_t)tracker_th, NULL);
    }
    else
    {
        // Turn the red LED on
        palSetPad(GPIOB, GPIOB_LED1);
    }

    /*
     * Mainloop keeps just a shell for us for configuring
     */
    while (TRUE) {
        if (!shelltp && (SDU.config->usbp->state == USB_ACTIVE)) {
            shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
        } else if (chThdTerminated(shelltp)) {
            chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
            shelltp = NULL;           /* Triggers spawning of a new shell.        */
        }
        chThdSleepMilliseconds(1000);
    }
}
