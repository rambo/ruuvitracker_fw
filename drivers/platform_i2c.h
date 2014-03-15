#ifndef _PLATFORM_I2C_H
#define _PLATFORM_I2C_H


typedef enum rt_error
{
    RT_ERR_OK = 0,
    RT_ERR_FAIL = -1, // Failure, not really an error, it might be normal and expected even, just an easier way to pass boolean status to the caller
    RT_ERR_TIMEOUT = -2, // obviously timeout
    RT_ERR_ERROR = -3 // General error
    // Add more error codes as we get there
    
} rt_error;

#define RT_DEFAULT_TIMEOUT (150) // ms
#define RT_TIMEOUT_INIT() unsigned int RT_TIMEOUT_STARTED = systick_get_raw();
#define RT_TIMEOUT_REINIT() RT_TIMEOUT_STARTED = systick_get_raw();
#define RT_TIMEOUT_CHECK(ms) if ((systick_get_raw() - RT_TIMEOUT_STARTED) > ms) { _DEBUG("timeout! systick_get_raw=%d RT_TIMEOUT_STARTED=%d, ms=%d\n", systick_get_raw(), RT_TIMEOUT_STARTED, ms); D_EXIT(); return RT_ERR_TIMEOUT; }

u32 platform_i2c_setup(u32 speed );
u32 platform_i2c_teardown();
rt_error platform_i2c_send_start();
rt_error platform_i2c_send_stop();
rt_error platform_i2c_send_address(u16 address, int direction );
rt_error platform_i2c_send_byte(u8 data );
rt_error platform_i2c_recv_byte(int ack, u8 *buff );



#endif
