#include "ch.h"
#include "hal.h"
#include "power.h"

/* Prototypes */
static void enable_ldo2(void);
static void disable_ldo2(void);
static void enable_ldo3(void);
static void disable_ldo3(void);
static void enable_ldo4(void);
static void disable_ldo4(void);
static void enable_gsm_fet(void);
static void disable_gsm_fet(void);

struct power_domains_t {
     enum POWER_DOMAIN domain;
     void (*enable)(void);
     void (*disable)(void);
     uint8_t used;
};

/* Define power domain handlers, MUST BE IN SAME ORDER as POWER_DOMAINS */
static struct power_domains_t power_domains[] = {
     { LDO2, enable_ldo2, disable_ldo2, 0 },
     { LDO3, enable_ldo3, disable_ldo3, 0 },
     { LDO4, enable_ldo4, disable_ldo4, 0 },
     { GSM, enable_gsm_fet, disable_gsm_fet, 0 },
};

static BinarySemaphore sem;
#define LOCK chBSemWait(&sem);
#define UNLOCK chBSemSignal(&sem);

void power_init(void)
{
     chBSemInit(&sem, FALSE);
}

void power_request(enum POWER_DOMAIN p)
{
     LOCK;
     if (!power_domains[p].used)
	  power_domains[p].enable();
     power_domains[p].used++;
     UNLOCK;
}

void power_release(enum POWER_DOMAIN p)
{
     LOCK;
     power_domains[p].used--;
     if (!power_domains[p].used)
	  power_domains[p].disable();
     UNLOCK;
}

/* Actual functions to enable or disable power */

static void enable_ldo2(void)
{
     palSetPad(GPIOC, GPIOC_ENABLE_LDO2);
}

static void disable_ldo2(void)
{
     palClearPad(GPIOC, GPIOC_ENABLE_LDO2);
}

static void enable_ldo3(void)
{
     palSetPad(GPIOC, GPIOC_ENABLE_LDO3);
}

static void disable_ldo3(void)
{
     palClearPad(GPIOC, GPIOC_ENABLE_LDO3);
}

static void enable_ldo4(void)
{
     palSetPad(GPIOC, GPIOC_ENABLE_LDO4);
}

static void disable_ldo4(void)
{
     palClearPad(GPIOC, GPIOC_ENABLE_LDO4);
}

static void enable_gsm_fet(void)
{
     palSetPad(GPIOC, GPIOC_ENABLE_GSM_VBAT);
}

static void disable_gsm_fet(void)
{
     palClearPad(GPIOC, GPIOC_ENABLE_GSM_VBAT);
}

void power_enter_stop(void)
{
    register_power_wakeup_callback(power_wakeup_callback);

    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR |= (PWR_CR_LPDS | PWR_CR_CSBF | PWR_CR_CWUF);
    PWR->CR &= ~PWR_CR_PDDS;
    __WFI();
}

void power_enter_standby(void)
{
    // Actually when coming back from standby we don't have this anymore since it's equivalent to a reset...
    register_power_wakeup_callback(power_wakeup_callback);

    chSysLock();
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    PWR->CR |= (PWR_CR_PDDS | PWR_CR_LPDS | PWR_CR_CSBF | PWR_CR_CWUF);
    RTC->ISR &= ~(RTC_ISR_ALRBF | RTC_ISR_ALRAF | RTC_ISR_WUTF | RTC_ISR_TAMP1F | RTC_ISR_TSOVF | RTC_ISR_TSF);
    __WFI();
}

void register_power_wakeup_callback(extcallback_t cb)
{
    EXTChannelConfig button_cfg;
    EXTChannelConfig rtc_cfg;

    button_cfg.cb = cb;
    button_cfg.mode = EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART | EXT_MODE_GPIOA;
    // PA0
    extSetChannelMode(&EXTD1, 0, &button_cfg);
    
    rtc_cfg.cb = cb;
    rtc_cfg.mode = EXT_CH_MODE_RISING_EDGE | EXT_CH_MODE_AUTOSTART;
    // RTC_ALARM
    extSetChannelMode(&EXTD1, 17, &rtc_cfg);
    // RTC_WKUP
    extSetChannelMode(&EXTD1, 22, &rtc_cfg);

    // I'm not sure if this is required
    extChannelEnable(&EXTD1, 0);
    extChannelEnable(&EXTD1, 17);
    extChannelEnable(&EXTD1, 22);
}

void power_wakeup_callback(EXTDriver *extp, expchannel_t channel)
{
    // TODO: Check if we actually were in STOP mode and only reinit clock in that case
    chSysLockFromIsr();
    stm32_clock_init();
    chSysUnlockFromIsr();
    // TODO: Raise an event (so other interested parties can know about the event that woke us up), for now just suppress the warnings
    (void)extp;
    (void)channel;
}
