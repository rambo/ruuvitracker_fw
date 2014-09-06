#include "acc.h"
#include "chtypes.h"
#include "hal.h"

// ### TODO: MOVE TO GLOBAL CONFIG SOMEWHERE
static const I2CConfig i2cfg1 = {
    OPMODE_I2C,
    400000,
    FAST_DUTY_CYCLE_2,
};

static const uint8_t ACC_I2C_ADDRESS = 0x1D;

// Register notes:
// 1) Register contents are preserved when a transition from ACTIVE to STANDBY mode occurs
// 2) Register contents are reset when a transition from STANDBY to ACTIVE mode occurs.
// 3) Register contents can be modified at any time in either STANDBY or ACTIVE mode.
//    A write to this register will cause a reset of the corresponding internal system debounce counter.
// 4) Register contents can only be modified while the device is in STANDBY mode; the only exceptions
//    to this are the CTRL_REG1[ACTIVE] and CTRL_REG2[RST] bits.

static const uint8_t ACC_STATUS = 0x00;
static const uint8_t ACC_ZYXDR_MASK = 1 << 3;

static const uint8_t ACC_OUT_X_MSB = 0x01;
static const uint8_t ACC_OUT_X_LSB = 0x02;
static const uint8_t ACC_OUT_Y_MSB = 0x03;
static const uint8_t ACC_OUT_Y_LSB = 0x04;
static const uint8_t ACC_OUT_Z_MSB = 0x05;
static const uint8_t ACC_OUT_Z_LSB = 0x06;

static const uint8_t ACC_CTRL_REG1 = 0x2A; // Register notes: 1,4
static const uint8_t ACC_ACTIVE_MASK    = 0b00000001;
static const uint8_t ACC_FREAD_MASK     = 0b00000010;
static const uint8_t ACC_FNOISE_MASK    = 0b00000100;
static const uint8_t ACC_ODR_MASK       = 0b00111000;
static const uint8_t ACC_SLEEP_ODR_MASK = 0b11000000;

static const uint8_t ACC_XYZ_DATA_CFG = 0x0E; // Register notes: 1,4
static const uint8_t ACC_RANGE_MASK = 0b00000011;

ACC_REPLY acc_read_register(uint8_t address, uint8_t *result, unsigned int count)
{
    i2cAcquireBus(&I2CD1);
    i2cStart(&I2CD1, &i2cfg1);

    msg_t status = i2cMasterTransmitTimeout(&I2CD1, ACC_I2C_ADDRESS, &address, 1, result, count, MS2ST(500));

    i2cStop(&I2CD1);
    i2cReleaseBus(&I2CD1);

    if (status == RDY_OK)
        return ACC_OK;

    // ### TODO: error handling
    return ACC_ERROR;
}

ACC_REPLY acc_write_register(uint8_t address, uint8_t value)
{
    i2cAcquireBus(&I2CD1);
    i2cStart(&I2CD1, &i2cfg1);

    uint8_t tx[2];
    tx[0] = address;
    tx[1] = value;

    msg_t status = i2cMasterTransmitTimeout(&I2CD1, ACC_I2C_ADDRESS, tx, sizeof(tx), 0, 0, MS2ST(500));

    i2cStop(&I2CD1);
    i2cReleaseBus(&I2CD1);

    if (status == RDY_OK)
        return ACC_OK;

    // ### TODO: error handling
    return ACC_ERROR;
}

ACC_REPLY acc_read_xyz_counts(int *x, int *y, int *z)
{
    // ### TODO: Support fast read (by only reading MSB registers)?
    uint8_t status;
    if (acc_read_register(ACC_STATUS, &status, 1) != ACC_OK)
        return ACC_ERROR;

    if (!(status & ACC_ZYXDR_MASK))
        return ACC_XYZ_DATA_NOT_READY;

    // Perform multibyte read of register 0x01-0x06
    uint8_t raw_result[6];
    if (acc_read_register(ACC_OUT_X_MSB, raw_result, sizeof(raw_result)) != ACC_OK)
        return ACC_ERROR;

    int *signed_result[3];
    signed_result[0] = x;
    signed_result[1] = y;
    signed_result[2] = z;

    int i;
    for (i = 0; i < 3; i++) {
        // Combine the two 8 bit registers (MSB and LSB) into one 12-bit number (left aligned):
        // D11 D10 D9 D8 D7 D6 D5 D4 D3 D2 D1 D0 x x x x
        int16_t counts = (uint16_t)(raw_result[i * 2] << 8) | (uint16_t)(raw_result[(i * 2) + 1]);
        // Then right align
        counts >>= 4;

        // Check if the number is negative, if so transform into negative 2's complement
        if (raw_result[i * 2] > 0x7F) {
            counts = ~counts + 1;
            counts *= -1;
        }

        if (signed_result[i])
            *signed_result[i] = counts;
    }

    return ACC_OK;
}

ACC_REPLY acc_read_xyz_g(float *x, float *y, float *z)
{
    int x_counts;
    int y_counts;
    int z_counts;

    int return_value = acc_read_xyz_counts(&x_counts, &y_counts, &z_counts);
    if (return_value != ACC_OK)
        return return_value;

    int range;
    if (acc_get_range(&range) != ACC_OK)
        return ACC_ERROR;

    const int counts_per_g = 1024 >> range;

    if (x)
        *x = (float)x_counts / (float)counts_per_g;
    if (y)
        *y = (float)y_counts / (float)counts_per_g;
    if (z)
        *z = (float)z_counts / (float)counts_per_g;

    return ACC_OK;
}

ACC_REPLY acc_get_mode(int *mode)
{
    uint8_t value;
    if (acc_read_register(ACC_CTRL_REG1, &value, 1) != ACC_OK)
        return ACC_ERROR;

    *mode = value & ACC_ACTIVE_MASK ? ACC_ACTIVE_MODE : ACC_STANDBY_MODE;
    return ACC_OK;
}

ACC_REPLY acc_set_mode(enum ACC_MODE mode)
{
    uint8_t value;
    if (acc_read_register(ACC_CTRL_REG1, &value, 1) != ACC_OK)
        return ACC_ERROR;

    if ((value & ACC_ACTIVE_MASK) == mode)
        return ACC_OK; // desired mode already set; nothing to do

    if (mode == ACC_ACTIVE_MODE)
        value |= ACC_ACTIVE_MASK;
    else
        value &= ~ACC_ACTIVE_MASK;

    return acc_write_register(ACC_CTRL_REG1, value);
}

ACC_REPLY acc_get_range(int *range)
{
    uint8_t value;
    if (acc_read_register(ACC_XYZ_DATA_CFG, &value, 1) != ACC_OK)
        return ACC_ERROR;

    value &= ACC_RANGE_MASK;
    *range = value;

    return ACC_OK;
}

ACC_REPLY acc_set_range(enum ACC_RANGE range)
{
    uint8_t value;
    if (acc_read_register(ACC_XYZ_DATA_CFG, &value, 1) != ACC_OK)
        return ACC_ERROR;

    if ((value & ACC_RANGE_MASK) == range)
        return ACC_OK; // desired range already set; nothing to do

    value &= ~ACC_RANGE_MASK;
    value |= range;

    // Store current mode (the range can only be changed while in standby mode)
    int mode;
    if (acc_get_mode(&mode) != ACC_OK)
        return ACC_ERROR;

    if (mode != ACC_STANDBY_MODE && acc_set_mode(ACC_STANDBY_MODE) != ACC_OK)
        return ACC_ERROR;

    if (acc_write_register(ACC_XYZ_DATA_CFG, value) != ACC_OK)
        return ACC_ERROR;

    // Reset mode
    if (mode != ACC_STANDBY_MODE && acc_set_mode(mode) != ACC_OK)
        return ACC_ERROR;

    return ACC_OK;
}

static ACC_REPLY acc_set_data_rate_helper(uint8_t mask, uint8_t rate)
{
    uint8_t value;
    if (acc_read_register(ACC_CTRL_REG1, &value, 1) != ACC_OK)
        return ACC_ERROR;

    if ((value & mask) == rate)
        return ACC_OK; // desired rate already set; nothing to do

    value &= ~mask;
    value |= rate;

    // Store current mode (data rate can only be changed while in standby mode)
    int mode;
    if (acc_get_mode(&mode) != ACC_OK)
        return ACC_ERROR;

    if (mode != ACC_STANDBY_MODE && acc_set_mode(ACC_STANDBY_MODE) != ACC_OK)
        return ACC_ERROR;

    if (acc_write_register(ACC_CTRL_REG1, value) != ACC_OK)
        return ACC_ERROR;

    // Reset mode
    if (mode != ACC_STANDBY_MODE && acc_set_mode(mode) != ACC_OK)
        return ACC_ERROR;

    return ACC_OK;
}

ACC_REPLY acc_set_data_rate(enum ACC_DATA_RATE rate)
{
    return acc_set_data_rate_helper(ACC_ODR_MASK, rate);
}

ACC_REPLY acc_set_auto_sleep_data_rate(enum ACC_AUTO_SLEEP_DATA_RATE rate)
{
    return acc_set_data_rate_helper(ACC_SLEEP_ODR_MASK, rate);
}
