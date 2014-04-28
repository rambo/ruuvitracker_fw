#include "drivers/testplatform.h"
#include "chprintf.h"
#include <stdlib.h>
#include "drivers/debug.h"

#define TP_PB_PIN 0


/**
 * initializes the pads used for testplatform syncs
 */
void tp_init(void)
{
    D_ENTER();
    // Make sure the pad is output
    _DEBUG("force PB%d mode\r\n", TP_PB_PIN);
    palSetPadMode(GPIOB, TP_PB_PIN, PAL_MODE_OUTPUT_PUSHPULL);
    // Make sure the pad is cleared...
    palClearPad(GPIOB, TP_PB_PIN);
    D_EXIT();
}

/**
 * Sends a pulse train of N 1ms (approx) sync pulses, starting and ending with 3ms (approx) pulses
 */
void tp_sync(uint8_t pulses)
{
    D_ENTER();
    uint8_t i;
    // Start pulsetrain
    _DEBUG("start train\r\n");
    palSetPad(GPIOB, TP_PB_PIN);
    chThdSleepMilliseconds(3);
    palClearPad(GPIOB, TP_PB_PIN);
    chThdSleepMilliseconds(1);
    for (i=0; i<pulses; i++)
    {
        _DEBUG("pulse %d\r\n", i);
        palSetPad(GPIOB, TP_PB_PIN);
        chThdSleepMilliseconds(1);
        palClearPad(GPIOB, TP_PB_PIN);
        chThdSleepMilliseconds(1);
    }
    // Stop pulsetrain
    _DEBUG("stop train\r\n");
    palSetPad(GPIOB, TP_PB_PIN);
    chThdSleepMilliseconds(3);
    palClearPad(GPIOB, TP_PB_PIN);
    D_EXIT();
}

/**
 * call tp_sync from shell
 */
void cmd_tp_sync(BaseSequentialStream *chp, int argc, char *argv[])
{
    uint8_t pulses;
    (void)argv;
    if (argc < 1)
    {
        goto ERROR;
    }

    pulses = atoi(argv[0]);
    chprintf(chp, "calling tp_sync(%d)\r\n", pulses);
    tp_sync(pulses);
    return;
    
ERROR:
    chprintf(chp, "Usage: tp_sync <pulses>\r\n");
}

/**
 * set pb0 state from shell
 */
void cmd_tp_set_syncpin(BaseSequentialStream *chp, int argc, char *argv[])
{
    uint8_t state;
    (void)argv;
    if (argc < 1)
    {
        goto ERROR;
    }

    state = atoi(argv[0]);
    if (state)
    {
        chprintf(chp, "Setting PB%d HIGH\r\n", TP_PB_PIN);
        palSetPad(GPIOB, TP_PB_PIN);
    }
    else
    {
        chprintf(chp, "Setting PB%d LOW\r\n", TP_PB_PIN);
        palClearPad(GPIOB, TP_PB_PIN);
    }

    return;
    
ERROR:
    chprintf(chp, "Usage: tp_set_syncpin 0/1\r\n");
}
