#include <inttypes.h>

enum ACC_REPLY {
    ACC_OK,
    ACC_XYZ_DATA_NOT_READY,
    ACC_ERROR
};
typedef enum ACC_REPLY ACC_REPLY;

enum ACC_MODE {
    ACC_STANDBY_MODE,
    ACC_ACTIVE_MODE
};

enum ACC_RANGE {
    ACC_2G_RANGE,
    ACC_4G_RANGE,
    ACC_8G_RANGE
};

enum ACC_DATA_RATE {
    ACC_800HZ_DATA_RATE   = 0b00000000,
    ACC_400HZ_DATA_RATE   = 0b00001000,
    ACC_200HZ_DATA_RATE   = 0b00010000,
    ACC_100HZ_DATA_RATE   = 0b00011000,
    ACC_50HZ_DATA_RATE    = 0b00100000,
    ACC_12_5HZ_DATA_RATE  = 0b00101000,
    ACC_6_25HZ_DATA_RATE  = 0b00110000,
    ACC_1_563HZ_DATA_RATE = 0b00111000
};

enum ACC_AUTO_SLEEP_DATA_RATE {
    // When the device is in auto-sleep mode, the system ODR and the data
    // rate for all the system functional blocks are overridden by the
    // data rate set by the ASLP_RATE field. Possible values:
    ACC_AUTO_SLEEP_50HZ_DATA_RATE   = 0b00000000,
    ACC_AUTO_SLEEP_12_5HZ_DATA_RATE = 0b01000000,
    ACC_AUTO_SLEEP_6_25HZ_DATA_RATE = 0b10000000,
    ACC_AUTO_SLEEP_1_56HZ_DATA_RATE = 0b11000000
};

ACC_REPLY acc_get_mode(int *mode);
ACC_REPLY acc_set_mode(enum ACC_MODE mode);

ACC_REPLY acc_read_register(uint8_t address, uint8_t *result, unsigned int count);

ACC_REPLY acc_read_xyz_counts(int *x, int *y, int *z);
ACC_REPLY acc_read_xyz_g(float *x, float *y, float *z);

ACC_REPLY acc_get_range(int *range);
ACC_REPLY acc_set_range(enum ACC_RANGE range);

ACC_REPLY acc_set_data_rate(enum ACC_DATA_RATE rate);
ACC_REPLY acc_set_auto_sleep_data_rate(enum ACC_AUTO_SLEEP_DATA_RATE rate);
