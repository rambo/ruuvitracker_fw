#ifndef TESTPLATFORM_H
#define TESTPLATFORM_H
#include "ch.h"
#include "hal.h"

void tp_init(void);

void tp_sync(uint8_t pulses);

void cmd_tp_sync(BaseSequentialStream *chp, int argc, char *argv[]);

void cmd_tp_set_syncpin(BaseSequentialStream *chp, int argc, char *argv[]);


#endif