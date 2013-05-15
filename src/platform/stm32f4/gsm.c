/*
 *  Simcom 908 GSM Driver for Ruuvitracker.
 *
 * @author: Seppo Takalo
 */


#include "platform.h"
#include "platform_conf.h"
#include "common.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "gsm.h"
#include <delay.h>
#include <string.h>
#include <stdlib.h>
#include <slre.h>

#ifdef BUILD_GSM

// For plaform_pio_op() portnums
#define PORT_A 0
#define PORT_B 1
#define PORT_C 2
#define PORT_D 3
#define PORT_E 4

#define STATUS_PIN GPIO_Pin_12
#define STATUS_PORT PORT_E
#define DTR_PIN    GPIO_Pin_14
#define DTR_PORT   PORT_C
#define POWER_PIN  GPIO_Pin_2
#define POWER_PORT PORT_E
#define ENABLE_PIN  GPIO_Pin_15
#define ENABLE_PORT PORT_C

#define BUFF_SIZE	64

#define GSM_CMD_LINE_END "\r\n"

#define TIMEOUT_MS   5000        /* Default timeout 5s */
#define TIMEOUT_HTTP 15000      /* Http timeout, 15s */

enum State {
  STATE_UNKNOWN = 0,
  STATE_OFF = 1,
  STATE_BOOTING,
  STATE_ASK_PIN,
  STATE_WAIT_NETWORK,
  STATE_READY,
  STATE_ERROR,
};
enum CFUN {
  CFUN_0 = 0,
  CFUN_1 = 1,
};
enum GSM_FLAGS {
  HW_FLOW_ENABLED = 0x01,
  SIM_INSERTED    = 0x02,
  GPS_READY       = 0x04,
  GPRS_READY      = 0x08,
  CALL            = 0x10,
  INCOMING_CALL   = 0x20,
};

/* Modem Status */
struct gsm_modem {
  enum Power_mode power_mode;
  enum State state;
  volatile int waiting_reply;
  volatile int raw_mode;
  enum Reply reply;
  int flags;
  enum CFUN cfun;
  int last_sms_index;
} static gsm = {		/* Initial status */
  .power_mode=POWER_OFF,
  .state = STATE_OFF,
};


/* Handler functions for AT replies*/
static void handle_ok(char *line);
static void handle_fail(char *line);
static void handle_error(char *line);
static void set_hw_flow();
static void parse_cfun(char *line);
static void gpsready(char *line);
static void sim_inserted(char *line);
static void no_sim(char *line);
static void incoming_call(char *line);
static void call_ended(char *line);
static void parse_network(char *line);
static void parse_sapbr(char *line);
static void parse_sms_in(char *line);

typedef struct Message Message;
struct Message {
  char *msg;			/* Message string */
  enum State next_state;	/* Atomatic state transition */
  void (*func)();		/* Function to call on message */
};

/* Messages from modem */
static Message urc_messages[] = {
  /* Unsolicited Result Codes (URC messages) */
  { "RDY",                  .next_state=STATE_BOOTING },
  { "+CPIN: NOT INSERTED",  .next_state=STATE_ERROR, .func = no_sim },
  { "+CPIN: READY",         .next_state=STATE_WAIT_NETWORK, .func = sim_inserted },
  { "+CPIN: SIM PIN",       .next_state=STATE_ASK_PIN, .func = sim_inserted },
  { "+CFUN:",               .func = parse_cfun },
  { "Call Ready",           .next_state=STATE_READY },
  { "GPS Ready",            .func = gpsready },
  { "NORMAL POWER DOWN",    .next_state=STATE_OFF },
  { "+COPS:",               .func = parse_network },
  { "+SAPBR:",              .func = parse_sapbr },
  /* Return codes */
  { "OK",   .func = handle_ok },
  { "FAIL", .func = handle_fail },
  { "ERROR",.func = handle_error },
  { "+CME ERROR", .func = handle_error },
  { "+CMS ERROR" },                       /* TODO: handle */
  /* During Call */
  { "NO CARRIER",   .func = call_ended }, /* This is general end-of-call */
  { "NO DIALTONE",  .func = call_ended },
  { "BUSY",         .func = handle_fail },
  { "NO ANSWER",    .func = handle_fail },
  { "RING",         .func = incoming_call },
  /* SMS */
  { "+CMTI:",       .func = parse_sms_in },
  { NULL } /* Table must end with NULL */
};

/* Some interrupt helpers */
int gsm_int_dummy_set_status( elua_int_resnum resnum, int state ) { return 0; }
int gsm_int_dummy_get_status( elua_int_resnum resnum ) { return ENABLE; }

int gsm_int_call_get_flag  ( elua_int_resnum resnum, int clear )
{
  return (gsm.flags&INCOMING_CALL)!=0;
}

int gsm_int_sms_get_flag  ( elua_int_resnum resnum, int clear )
{
//TODO: implement
  return 0;
}

static void incoming_call(char *line)
{
  gsm.flags |= INCOMING_CALL;
  cmn_int_handler( INT_GSM_CALL, 0 );
}

static void call_ended(char *line)
{
  gsm.flags &= ~CALL;
  gsm.flags &= ~INCOMING_CALL;
}

static void parse_sms_in(char *line)
{
  if (1 == sscanf(line, "+CMTI: \"SM\",%d", &gsm.last_sms_index)) {
    cmn_int_handler( INT_GSM_SMS, gsm.last_sms_index);
  }
}

static void parse_network(char *line)
{
  char network[64];
  /* Example: +COPS: 0,0,"Saunalahti" */
  if (1 == sscanf(line, "+COPS: 0,0,\"%s", network)) {
    *strchr(network,'"') = 0;
    printf("GSM: Registered to network %s\n", network);
    gsm.state = STATE_READY;
  }
}

#define STATUS_CONNECTING 0
#define STATUS_CONNECTED 1
static const char sapbr_deact[] = "+SAPBR 1: DEACT";

static void parse_sapbr(char *line)
{
  int status;

  /* Example: +SAPBR: 1,1,"10.172.79.111"
   * 1=Profile number
   * 1=connected, 0=connecting, 3,4 =closing/closed
   * ".." = ip addr
   */
  if (1 == sscanf(line, "+SAPBR: %*d,%d", &status)) {
    switch(status) {
    case STATUS_CONNECTING:
    case STATUS_CONNECTED:
      gsm.flags |= GPRS_READY;
      break;
    default:
      gsm.flags &= ~GPRS_READY;
    }
  } else if (0 == strncmp(line, sapbr_deact, strlen(sapbr_deact))) {
    gsm.flags &= ~GPRS_READY;
  }
}

static void sim_inserted(char *line)
{
  gsm.flags |= SIM_INSERTED;
}

static void no_sim(char *line)
{
  gsm.flags &= ~SIM_INSERTED;
}

static void gpsready(char *line)
{
  gsm.flags |= GPS_READY;
}

static void parse_cfun(char *line)
{
  if (strchr(line,'0')) {
    gsm.cfun = CFUN_0;
  } else if (strchr(line,'1')) {
    gsm.cfun = CFUN_1;
  } else {
    printf("GSM: Unknown CFUN state\n");
  }
}
static void handle_ok(char *line)
{
  gsm.reply = AT_OK;
  gsm.waiting_reply = 0;
}

static void handle_fail(char *line)
{
  gsm.reply = AT_FAIL;
  gsm.waiting_reply = 0;
}

static void handle_error(char *line)
{
  gsm.reply = AT_ERROR;
  gsm.waiting_reply = 0;
}


/**
 * Send AT command to modem.
 * Waits for modem to reply with 'OK', 'FAIL' or 'ERROR'
 * return AT_OK, AT_FAIL or AT_ERROR
 */
int gsm_cmd(const char *cmd)
{
  unsigned int t;

  /* Flush buffers */
  gsm_disable_raw_mode();
  while(-1 != platform_s_uart_recv(GSM_UART_ID, 0))
    ;;

  t = systick_get_raw();                  /* Get System timer current value */
  t += TIMEOUT_MS*systick_get_hz()/1000;
  gsm.waiting_reply = 1;
  gsm_uart_write(cmd);
  gsm_uart_write(GSM_CMD_LINE_END);

  while(gsm.waiting_reply) {
    if (t < systick_get_raw()) {
      /* Timeout */
      gsm.waiting_reply = 0;
      return AT_TIMEOUT;
    }
    delay_ms(1);
  }

  return gsm.reply;
}

void gsm_set_raw_mode()
{
  gsm.raw_mode = 1;
}
void gsm_disable_raw_mode()
{
  gsm.raw_mode = 0;
}
int gsm_is_raw_mode()
{
  return gsm.raw_mode;
}

/* Wait for specific pattern, copy rest of line to buffer, if given*/
int gsm_wait(const char *pattern, int timeout, char *line)
{
  const char *p = pattern;
  int c, ret;
  int i;
  unsigned int t = systick_get_raw();
  t+=timeout*systick_get_hz()/1000;

  int was_raw_mode = gsm_is_raw_mode();
  gsm_set_raw_mode();

  ret = AT_OK;
  while(*p) {
    c = platform_s_uart_recv(GSM_UART_ID, 0);
    if (-1 == c) {
      if (t < systick_get_raw()) {
        ret = AT_TIMEOUT;
        goto WAIT_END;
      } else {
        delay_ms(1);
      }
      continue;
    }
    if (c == *p) { //Match
      p++;
    } else { //No match, go back to start
      p = pattern;
    }
  }

  if (line) {
       strcpy(line, pattern);
       i = strlen(line);
       while ('\n' != (c = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT))) {
	    line[i++] = c;
       }
  }

WAIT_END:
  if (!was_raw_mode)
    gsm_disable_raw_mode();

  return ret;
}

/* Read line from GSM serial port to buffer */
int gsm_read_line(char *buf, int max_len)
{
  int i,c;
  int was_raw = gsm_is_raw_mode();
  gsm_set_raw_mode();
  for(i=0;i<(max_len-1);i++) {
    c = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
    buf[i] = (char)c;
    if ('\n' == c)
      break;
  }
  buf[++i]=0;
  if (!was_raw)
    gsm_disable_raw_mode();
  return i;
}

int gsm_read_raw(char *buf, int max_len)
{
  int i;
  int was_raw = gsm_is_raw_mode();
  gsm_set_raw_mode();
  for(i=0;i<max_len;i++) {
    buf[i] = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
  }
  if (!was_raw)
    gsm_disable_raw_mode();
  return i;
}

/* Setup IO ports. Called in platform_setup() from platform.c */
void gsm_setup_io()
{
  /* Power pin (PE.2) */
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_SET);
  /* DTR pin (PC14) */
  platform_pio_op(DTR_PORT, DTR_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(DTR_PORT, DTR_PIN, PLATFORM_IO_PIN_CLEAR);
  /* Status pin (PE12) */
  platform_pio_op(STATUS_PORT, STATUS_PIN, PLATFORM_IO_PIN_DIR_INPUT);
  /* Enable_voltage (PC15) */
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_DIR_OUTPUT);
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);

  // Serial port
  platform_uart_setup( GSM_UART_ID, 115200, 8, PLATFORM_UART_PARITY_NONE, PLATFORM_UART_STOPBITS_1);
  platform_s_uart_set_flow_control( GSM_UART_ID, PLATFORM_UART_FLOW_CTS | PLATFORM_UART_FLOW_RTS);
}

static void set_hw_flow()
{
  while(gsm.state < STATE_BOOTING)
    delay_ms(1);

  if (AT_OK == gsm_cmd("AT+IFC=2,2")) {
    gsm.flags |= HW_FLOW_ENABLED;
    gsm_cmd("ATE0"); 		/* Disable ECHO */
  }
}

void gsm_enable_voltage()
{
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);
  delay_ms(100);		/* Give 100ms for voltage to settle */
}

void gsm_disable_voltage()
{
  platform_pio_op(ENABLE_PORT, ENABLE_PIN, PLATFORM_IO_PIN_CLEAR);
}

void gsm_toggle_power_pin()
{
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_CLEAR);
  delay_ms(2000);
  platform_pio_op(POWER_PORT, POWER_PIN, PLATFORM_IO_PIN_SET);
}

/* Send *really* slow. Add inter character delays */
static void slow_send(const char *line) {
  while(*line) {
      platform_s_uart_send(GSM_UART_ID, *line);
    delay_ms(10);		/* inter-char delay */
     if ('\r' == *line)
       delay_ms(100);		/* Inter line delay */
     line++;
  }
}

void gsm_uart_write(const char *line)
{
  if (!gsm.flags&HW_FLOW_ENABLED)
    return slow_send(line);

  while(*line) {
    platform_s_uart_send(GSM_UART_ID, *line);
    line++;
  }
}

/* Find message matching current line */
static Message *lookup_urc_message(const char *line)
{
  int n;
  for(n=0; urc_messages[n].msg; n++) {
    if (0 == strncmp(line, urc_messages[n].msg, strlen(urc_messages[n].msg))) {
      return &urc_messages[n];
    }
  }
  return NULL;
}

/**
 * GSM command and state machine handler.
 * Receives lines from Simcom serial interfaces and parses them.
 */
void gsm_line_received()
{
  Message *m;
  char buf[BUFF_SIZE];
  int c,i=0;

  /* Dont mess with patterns */
  if (gsm_is_raw_mode())
    return;

  while('\n' != (c=platform_s_uart_recv(GSM_UART_ID,0))) {
    if (-1 == c)
      break;
    if ('\r' == c)
      continue;
    buf[i++] = (char)c;
    if(i==BUFF_SIZE)
      break;
  }
  buf[i] = 0;
  /* Skip empty lines */
  if (0 == i)
    return;
  if (0 == strcmp(GSM_CMD_LINE_END, buf))
    return;

  printf("GSM: %s\n", buf);

  m = lookup_urc_message(buf);
  if (m) {
    if (m->next_state) {
      gsm.state = m->next_state;
    }
    if (m->func) {
      m->func(buf);
    }
  }
}

/* Enable GSM module */
void gsm_set_power_state(enum Power_mode mode)
{
  int status_pin = platform_pio_op(STATUS_PORT, STATUS_PIN, PLATFORM_IO_PIN_GET);

  switch(mode) {
  case POWER_ON:
    if (0 == status_pin) {
      gsm_enable_voltage();
      gsm_toggle_power_pin();
      set_hw_flow();
    } else {                    /* Modem already on. Possibly warm reset */
      if (gsm.state == STATE_OFF) {
        gsm_cmd("AT+CPIN?");    /* Check PIN, Functionality and Network status */
        gsm_cmd("AT+CFUN?");    /* Responses of these will feed the state machine */
        gsm_cmd("AT+COPS?");
        gsm_cmd("AT+SAPBR=2,1"); /* Query GPRS status */
        set_hw_flow();
        gsm_cmd("ATE0");
        /* We should now know modem's real status */
        /* Assume that gps is ready, there is no good way to check it */
        gsm.flags |= GPS_READY;
      }
    }
    break;
  case POWER_OFF:
    if (1 == status_pin) {
      gsm_toggle_power_pin();
    }
    break;
  }
}

/* Check if GPS flag is set */
int gsm_is_gps_ready()
{
  if((gsm.flags&GPS_READY) != 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/* -------------------- L U A   I N T E R F A C E --------------------*/

/* COMMON FUNCTIONS */

static int gsm_lua_set_power_state(lua_State *L)
{
  enum Power_mode next = luaL_checkinteger(L, -1);
  gsm_set_power_state(next);
  return 0;
}

static int gsm_power_on(lua_State *L)
{
  gsm_set_power_state(POWER_ON);
  return 0;
}

static int gsm_power_off(lua_State *L)
{
  gsm_set_power_state(POWER_OFF);
  return 0;
}

static int gsm_send_cmd(lua_State *L)
{
  const char *cmd;
  int ret;

  cmd = luaL_checklstring(L, -1, NULL);
  if (!cmd)
    return 0;

  ret = gsm_cmd(cmd);
  lua_pushinteger(L, ret);

  if (ret != AT_OK) {
    printf("GSM: Cmd failed (%s) returned %d\n", cmd, ret);
  }
  return 1;
}

static int gsm_is_pin_required(lua_State *L)
{
  /* Wait for modem to boot */
  while( gsm.state < STATE_ASK_PIN )
    delay_ms(1);		/* Allow uC to sleep */

  lua_pushboolean(L, STATE_ASK_PIN == gsm.state);
  return 1;
}

static int gsm_send_pin(lua_State *L)
{
  int ret;
  char cmd[20];
  const char *pin = luaL_checklstring(L, -1, NULL);
  if (!pin)
    return 0;
  snprintf(cmd,20,"AT+CPIN=%s", pin);

  /* Wait for modem to boot */
  while( gsm.state < STATE_ASK_PIN )
    delay_ms(1);		/* Allow uC to sleep */

  if (STATE_ASK_PIN == gsm.state) {
    ret = gsm_cmd(cmd);
    lua_pushinteger(L, ret);
  } else {
    lua_pushinteger(L, AT_OK);
  }
  return 1;
}

static int gsm_gprs_enable(lua_State *L)
{
  const char *apn = luaL_checkstring(L, 1);
  char ap_cmd[64];

  /* Wait for CFUN=1 (Radio on)*/
  while(gsm.cfun != CFUN_1)
    delay_ms(TIMEOUT_MS);

  /* Check if already enabled */
  gsm_cmd("AT+SAPBR=2,1");

  if (gsm.flags&GPRS_READY)
    return 0;

  snprintf(ap_cmd, 64, "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);

  while (gsm.state < STATE_READY)
    delay_ms(1000);

  gsm_cmd("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
  gsm_cmd(ap_cmd);
  gsm_cmd("AT+SAPBR=1,1");

  if (AT_OK == gsm.reply)
    gsm.flags |= GPRS_READY;
  return gsm.reply;
}


static int gsm_lua_wait(lua_State *L)
{
  int ret,timeout=TIMEOUT_MS;
  char line[256];
  const char *text;
  text = luaL_checklstring(L, 1, NULL);
  if (2 <= lua_gettop(L)) {
       timeout = luaL_checkinteger(L, 2);
  }
  ret = gsm_wait(text, timeout, line);
  lua_pushinteger(L, ret);
  lua_pushstring(L, line);
  return 2;
}

static int gsm_state(lua_State *L)
{
  lua_pushinteger(L, gsm.state);
  return 1;
}


static int gsm_is_ready(lua_State *L)
{
  lua_pushboolean(L, gsm.state == STATE_READY);
  return 1;
}

static int gsm_flag_is_set(lua_State *L)
{
  int flag = luaL_checkinteger(L, -1);
  lua_pushboolean(L, (gsm.flags&flag) != 0);
  return 1;
}

static int gsm_get_caller(lua_State *L)
{
  char line[128];
  char number[64];
  gsm_set_raw_mode();
  gsm_uart_write("AT+CLCC" GSM_CMD_LINE_END);
  while(1) {
    gsm_read_line(line, 128);
    if(1 == sscanf(line, "+CLCC: %*d,%*d,4,0,%*d,\"%s", number)) {
      *strchr(number,'"') = 0;
      lua_pushstring(L, number);
      gsm_disable_raw_mode();
      return 1;
    }
    if(0 == strcmp(line, "OK\r\n")) /* End of responses */
      break;
  }
  gsm_disable_raw_mode();
  return 0;
}


/* SMS FUNCTIONS */

static const char ctrlZ[] = {26, 0};
static int gsm_send_sms(lua_State *L)
{
  const char *number;
  const char *text;
  int ret;
  
  number = luaL_checklstring(L, -2, NULL);
  text = luaL_checklstring(L, -1, NULL);
  if (!number || !text)
    return 0;

  if (AT_OK != gsm_cmd("AT+CMGF=1")) {		/* Set to text mode */
    lua_pushinteger(L, AT_ERROR);
    return 1;
  }

  gsm_uart_write("AT+CMGS=\"");
  gsm_uart_write(number);

  gsm_uart_write("\"\r");
  gsm_wait(">", TIMEOUT_MS, NULL);	 /* Send SMS cmd and wait for '>' to appear */
  gsm_uart_write(text);
  ret = gsm_cmd(ctrlZ);		/* CTRL-Z ends message. Wait for 'OK' */
  lua_pushinteger(L, ret);
  return 1;
}

static int gsm_read_sms(lua_State *L)
{
  char tmp[256], msg[161], number[100];
  const char *err;
  int r;
  int index = luaL_checkinteger(L, 1);
  gsm_cmd("AT+CMGF=1");         /* Set to text mode */
  gsm_set_raw_mode();
  snprintf(tmp, 161, "AT+CMGR=%d" GSM_CMD_LINE_END, index);
  gsm_uart_write(tmp);
  r = gsm_wait("+CMGR:", TIMEOUT_MS, tmp);
  if (AT_OK != r) {
    printf("GSM: timeout\n");
    gsm_disable_raw_mode();
    return 0;
  }
  /* Example: +CMGR: "REC READ","+358403445818","","13/05/13,18:00:15+12" */
  err = slre_match(0, "^\\+CMGR:\\s\"[^\"]+\",\"([^\"]+)", tmp, strlen(tmp),
                   SLRE_STRING, sizeof(number), number);
  if (NULL != err) {
    printf("GSM: Error parsing \"%s\": %s\n", tmp, err);
    gsm_disable_raw_mode();
    return 0;
  }
  msg[0] = 0;
  tmp[0] = 0;
  do {
    gsm_read_line(tmp, sizeof(tmp));
    if (0 == strcmp(GSM_CMD_LINE_END, tmp)) /* Stop at empty line */
      break;
    strcat(msg, tmp);
  } while(1);
  gsm_wait("OK", TIMEOUT_MS, tmp);       /* Read response to clear buffer */
  gsm_disable_raw_mode();

  /* Response table */
  lua_newtable(L);
  lua_pushstring(L, "from");
  lua_pushstring(L, number);
  lua_settable(L, -3);
  lua_pushstring(L, "text");
  lua_pushstring(L, msg);
  lua_settable(L, -3);
  return 1;
}

static int gsm_delete_sms(lua_State *L)
{
  char cmd[20];
  int ret;
  int index = luaL_checkinteger(L, 1);
  snprintf(cmd,64,"AT+CMGD=%d", index);
  ret = gsm_cmd(cmd);
  lua_pushinteger(L, ret);
  return 1;
}


/* HTTP FUNCTIONS */

typedef enum method_t { GET, POST } method_t; /* HTTP methods */

static int gsm_http_init(const char *url)
{
  int ret;
  char url_cmd[256];
  snprintf(url_cmd, 256, "AT+HTTPPARA=\"URL\",\"%s\"", url);
  ret = gsm_cmd("AT+HTTPINIT");
  if (ret != AT_OK)
    return -1;
  ret = gsm_cmd("AT+HTTPPARA=\"CID\",\"1\"");
  if (ret != AT_OK)
    return -1;
  ret = gsm_cmd(url_cmd);
  if (ret != AT_OK)
    return -1;
  ret = gsm_cmd("AT+HTTPPARA=\"UA\",\"RuuviTracker/SIM908\"");
  if (ret != AT_OK)
    return -1;
  ret = gsm_cmd("AT+HTTPPARA=\"REDIR\",\"1\"");
  if (ret != AT_OK)
    return -1;
  return 0;
}

static int gsm_http_send_content_type(const char *content_type)
{
  char cmd[256];
  snprintf(cmd,256,"AT+HTTPPARA=\"CONTENT\",\"%s\"", content_type);
  if (gsm_cmd(cmd) != AT_OK)
    return -1;
  return 0;
}

static int gsm_http_send_data(const char *data)
{
  char cmd[256];
  snprintf(cmd, 256, "AT+HTTPDATA=%d,1000" GSM_CMD_LINE_END, strlen(data));
  gsm_uart_write(cmd);
  if (gsm_wait("DOWNLOAD", TIMEOUT_MS, NULL) == AT_TIMEOUT) {
    gsm.reply = AT_TIMEOUT;
    return -1;
  }
  gsm_uart_write(data);
  if (gsm_cmd(GSM_CMD_LINE_END) != AT_OK)  /* Send empty command to wait for OK */
    return -1;
  return 0;
}

static int gsm_http_handle(lua_State *L, method_t method,
                    const char *data, const char *content_type)
{
  int i,status,len,ret;
  char resp[64];
  const char *url = luaL_checkstring(L, 1);
  const char *p,*httpread_pattern = "+HTTPREAD";
  char *buf=0;
  char c;

  if (gsm_http_init(url) != 0) {
    printf("GSM: Failed to initialize HTTP\n");
    goto HTTP_END;
  }

  if (content_type) {
    if (gsm_http_send_content_type(content_type) != 0) {
      printf("GSM: Failed to set content type\n");
      goto HTTP_END;
    }
  }

  if (data) {
    if (gsm_http_send_data(data) != 0) {
      printf("GSM: Failed to send http data\n");
      goto HTTP_END;
    }
  }

  if (GET == method)
    ret = gsm_cmd("AT+HTTPACTION=0");
  else
    ret = gsm_cmd("AT+HTTPACTION=1");
  if (ret != AT_OK) {
    printf("GSM: HTTP Action failed\n");
    goto HTTP_END;
  }

  if (gsm_wait("+HTTPACTION", TIMEOUT_HTTP, resp) == AT_TIMEOUT) {
    printf("GSM: HTTP Timeout\n");
    goto HTTP_END;
  }

  if (2 != sscanf(resp, "+HTTPACTION:%*d,%d,%d", &status, &len)) { /* +HTTPACTION:<method>,<result>,<lenght of data> */
    printf("GSM: Failed to parse response\n");
    goto HTTP_END;
  }

  /* Read response, if there is any */
  if (len < 0)
    goto HTTP_END;

  if (NULL == (buf = malloc(len))) {
    printf("GSM: Out of memory\n");
    goto HTTP_END;
  }
  
  //Now read all bytes. block others from reading.
  gsm_set_raw_mode();
  gsm_uart_write("AT+HTTPREAD" GSM_CMD_LINE_END); //Wait for header
  p = httpread_pattern;
  while(*p) {           /* Wait for "+HTTPREAD:<data_len>" */
    c = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
    if (c == *p) { //Match
      p++;
    } else { //No match, go back to start
      p = httpread_pattern;
    }
  }
  while ('\n' != platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT))
    ;;                          /* Wait end of line */
  /* Rest of bytes are data */
  for(i=0;i<len;i++) {
    buf[i] = platform_s_uart_recv(GSM_UART_ID, PLATFORM_TIMER_INF_TIMEOUT);
  }
  buf[i]=0;
  gsm_disable_raw_mode();

HTTP_END:
  gsm_cmd("AT+HTTPTERM");

  /* Create response structure */
  lua_newtable(L);
  lua_pushstring(L, "status");
  lua_pushinteger(L, status);
  lua_settable(L, -3);
  if (buf) {
    lua_pushstring(L, "content");
    lua_pushstring(L, buf);
    lua_settable(L, -3);
  }
  lua_pushstring(L, "is_success" );
  lua_pushboolean(L, ((200 <= status) && (status < 300))); /* If HTTP response was 2xx */
  lua_settable(L, -3);
  return 1;
}

static int gsm_http_get(lua_State *L)
{
  return gsm_http_handle(L, GET, NULL, NULL);
}

static int gsm_http_post(lua_State *L)
{
  const char *data = luaL_checkstring(L, 2); /* 1 is url, it is handled in http_handle function */
  const char *content_type = luaL_checkstring(L, 3);
  return gsm_http_handle(L, POST, data, content_type);
}


/* Export Lua GSM library */

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"

const LUA_REG_TYPE gsm_map[] =
{
#if LUA_OPTIMIZE_MEMORY > 0
  /* Functions, Map name to func (I was lazy)*/
#define F(name,func) { LSTRKEY(#name), LFUNCVAL(func) }
  F(set_power_state, gsm_lua_set_power_state) ,
  F(is_ready, gsm_is_ready),
  F(flag_is_set, gsm_flag_is_set),
  F(state, gsm_state),
  F(is_pin_required, gsm_is_pin_required),
  F(send_pin, gsm_send_pin),
  F(send_sms, gsm_send_sms),
  F(read_sms, gsm_read_sms),
  F(delete_sms, gsm_delete_sms),
  F(cmd, gsm_send_cmd),
  F(wait, gsm_lua_wait),
  F(gprs_enable, gsm_gprs_enable),
  F(power_on, gsm_power_on),
  F(power_off, gsm_power_off),
  F(get_caller, gsm_get_caller),

  /* CONSTANTS */
#define MAP(a) { LSTRKEY(#a), LNUMVAL(a) }
  MAP( POWER_ON ),
  MAP( POWER_OFF ),
  MAP( STATE_OFF),
  MAP( STATE_BOOTING ),
  MAP( STATE_ASK_PIN ),
  MAP( STATE_WAIT_NETWORK ),
  MAP( STATE_READY ),
  MAP( STATE_ERROR ),
  MAP( GPS_READY ),
  MAP( SIM_INSERTED ),
  MAP( GPRS_READY ),
  MAP( INCOMING_CALL ),
  { LSTRKEY("OK"), LNUMVAL(AT_OK) },
  { LSTRKEY("FAIL"), LNUMVAL(AT_FAIL) },
  { LSTRKEY("ERROR"), LNUMVAL(AT_ERROR) },
  { LSTRKEY("TIMEOUT"), LNUMVAL(AT_TIMEOUT) },
#endif
  { LNILKEY, LNILVAL }
};

/* Add HTTP related functions to another table */
const LUA_REG_TYPE http_map[] =
{
  F(get, gsm_http_get),
  F(post, gsm_http_post),
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_gsm( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else
#error "Optimize memory=0 is not supported"
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}

#endif	/* BUILD_GSM */
