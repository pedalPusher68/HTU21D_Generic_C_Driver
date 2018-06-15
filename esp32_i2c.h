//
// Created by Bradley Smith on 6/12/18.
//

#ifndef ESP32_APP_ENV_MONITOR_ESP32_I2C_H
#define ESP32_APP_ENV_MONITOR_ESP32_I2C_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * The header "i2c.h" has to be implemented for your own platform to // brad -> changed to esp32_i2c.h
 * conform the following protocol :
 *
 * enum i2c_transfer_direction {
 * 	I2C_TRANSFER_WRITE = 0,
 * 	I2C_TRANSFER_READ  = 1,
 * };
 *
 * enum status_code {
 * 	STATUS_OK           = 0x00,
 * 	STATUS_ERR_OVERFLOW	= 0x01,
 *		STATUS_ERR_TIMEOUT  = 0x02,
 * };
 *
 * struct i2c_master_packet {
 * 	// Address to slave device
 * 	uint16_t address;
 * 	// Length of data array
 * 	uint16_t data_length;
 * 	// Data array containing all data to be transferred
 * 	uint8_t *data;
 * };
 *
 * void i2c_master_init(void);
 * enum status_code i2c_master_read_packet_wait(struct i2c_master_packet *const packet);
 * enum status_code i2c_master_write_packet_wait(struct i2c_master_packet *const packet);
 * enum status_code i2c_master_write_packet_wait_no_stop(struct i2c_master_packet *const packet);
 */
enum i2c_transfer_direction {
    I2C_TRANSFER_WRITE = 0,
    I2C_TRANSFER_READ = 1,
};

enum status_code {
    STATUS_OK = 0x00,
    STATUS_ERR_OVERFLOW = 0x01,
    STATUS_ERR_TIMEOUT = 0x02,
};

struct i2c_master_packet {
    // Address to slave device
    uint16_t address;
    // Length of data array
    uint16_t data_length;
    // Data array containing all data to be transferred
    uint8_t *data;
};

void i2c_master_init(void);

void delay_ms(int ms);

enum status_code i2c_master_read_packet_wait(struct i2c_master_packet *const packet);

enum status_code i2c_master_write_packet_wait(struct i2c_master_packet *const packet);

enum status_code i2c_master_write_packet_wait_no_stop(struct i2c_master_packet *const packet);


#endif //ESP32_APP_ENV_MONITOR_ESP32_I2C_H
