/**
 * \file htu21d.c
 *
 * \brief HTU21 Temperature & Humidity sensor driver source file
 *
 * Copyright (c) 2016 Measurement Specialties. All rights reserved.
 *
 * For details on programming, refer to htu21 datasheet :
 * http://www.meas-spec.com/downloads/HTU21D.pdf
 *
 */

#include "htu21d.h"

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
#include "esp32_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

// HTU21 device address
#define HTU21_ADDR											0x40 //0b1000000

// HTU21 device commands
#define HTU21_RESET_COMMAND									0xFE
#define HTU21_READ_TEMPERATURE_W_HOLD_COMMAND				0xE3
#define HTU21_READ_TEMPERATURE_WO_HOLD_COMMAND				0xF3
#define HTU21_READ_HUMIDITY_W_HOLD_COMMAND					0xE5
#define HTU21_READ_HUMIDITY_WO_HOLD_COMMAND					0xF5
#define HTU21_READ_SERIAL_FIRST_8BYTES_COMMAND				0xFA0F
#define HTU21_READ_SERIAL_LAST_6BYTES_COMMAND				0xFCC9
#define HTU21_WRITE_USER_REG_COMMAND						0xE6
#define HTU21_READ_USER_REG_COMMAND							0xE7

#define RESET_TIME											15			// ms value

// Processing constants
#define HTU21_TEMPERATURE_COEFFICIENT						(float)(-0.15)
#define HTU21_CONSTANT_A									(float)(8.1332)
#define HTU21_CONSTANT_B									(float)(1762.39)
#define HTU21_CONSTANT_C									(float)(235.66)

// Coefficients for temperature computation
#define TEMPERATURE_COEFF_MUL								(175.72)
#define TEMPERATURE_COEFF_ADD								(-46.85)

// Coefficients for relative humidity computation
#define HUMIDITY_COEFF_MUL									(125)
#define HUMIDITY_COEFF_ADD									(-6)

// Conversion timings
#define HTU21_TEMPERATURE_CONVERSION_TIME_T_14b_RH_12b		50000
#define HTU21_TEMPERATURE_CONVERSION_TIME_T_13b_RH_10b		25000
#define HTU21_TEMPERATURE_CONVERSION_TIME_T_12b_RH_8b		13000
#define HTU21_TEMPERATURE_CONVERSION_TIME_T_11b_RH_11b		7000
#define HTU21_HUMIDITY_CONVERSION_TIME_T_14b_RH_12b			16000
#define HTU21_HUMIDITY_CONVERSION_TIME_T_13b_RH_10b			5000
#define HTU21_HUMIDITY_CONVERSION_TIME_T_12b_RH_8b			3000
#define HTU21_HUMIDITY_CONVERSION_TIME_T_11b_RH_11b			8000

// HTU21 User Register masks and bit position
#define HTU21_USER_REG_RESOLUTION_MASK						0x81
#define HTU21_USER_REG_END_OF_BATTERY_MASK					0x40
#define HTU21_USER_REG_ENABLE_ONCHIP_HEATER_MASK			0x4
#define HTU21_USER_REG_DISABLE_OTP_RELOAD_MASK				0x2
#define HTU21_USER_REG_RESERVED_MASK						(~(		HTU21_USER_REG_RESOLUTION_MASK			\
																|	HTU21_USER_REG_END_OF_BATTERY_MASK		\
																|	HTU21_USER_REG_ENABLE_ONCHIP_HEATER_MASK	\
																|	HTU21_USER_REG_DISABLE_OTP_RELOAD_MASK ))

// HTU User Register values
// Resolution
#define HTU21_USER_REG_RESOLUTION_T_14b_RH_12b				0x00
#define HTU21_USER_REG_RESOLUTION_T_13b_RH_10b				0x80
#define HTU21_USER_REG_RESOLUTION_T_12b_RH_8b				0x01
#define HTU21_USER_REG_RESOLUTION_T_11b_RH_11b				0x81

// End of battery status
#define HTU21_USER_REG_END_OF_BATTERY_VDD_ABOVE_2_25V		0x00
#define HTU21_USER_REG_END_OF_BATTERY_VDD_BELOW_2_25V		0x40
// Enable on chip heater
#define HTU21_USER_REG_ONCHIP_HEATER_ENABLE					0x04
#define HTU21_USER_REG_OTP_RELOAD_DISABLE					0x02

uint32_t htu21_temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_14b_RH_12b;
uint32_t htu21_humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_14b_RH_12b;
enum htu21_i2c_master_mode i2c_master_mode;

// Static functions
static enum htu21_status htu21_write_command(uint8_t);
static enum htu21_status htu21_write_command_no_stop(uint8_t);
static enum htu21_status htu21_read_user_register(uint8_t *);
static enum htu21_status htu21_write_user_register(uint8_t );
static enum htu21_status htu21_temperature_conversion_and_read_adc( uint16_t *);
static enum htu21_status htu21_humidity_conversion_and_read_adc( uint16_t *);
static enum htu21_status htu21_crc_check( uint16_t, uint8_t);

/**
 * \brief Configures the SERCOM I2C master to be used with the htu21 device.
 */
void htu21_init(void)
{
	i2c_master_mode = htu21_i2c_no_hold;
	
	/* Initialize and enable device with config. */
	i2c_master_init();
}

/**
 * \brief Check whether HTU21 device is connected
 *
 * \return bool : status of HTU21
 *       - true : Device is present
 *       - false : Device is not acknowledging I2C address
  */
bool htu21_is_connected(void)
{
	enum status_code i2c_status;
	
	struct i2c_master_packet transfer = {
		.address     = HTU21_ADDR,
		.data_length = 0,
		.data        = NULL,
	};
	/* Do the transfer */
	i2c_status = i2c_master_write_packet_wait(&transfer);
	if( i2c_status != STATUS_OK)
		return false;
	
	return true;
}
	
/**
 * \brief Reset the HTU21 device
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status  htu21_reset(void)
{
	enum htu21_status status;
	
	status = htu21_write_command(HTU21_RESET_COMMAND);
	if( status != htu21_status_ok )
		return status;
	
	htu21_temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_14b_RH_12b;
	htu21_humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_14b_RH_12b;

	delay_ms(RESET_TIME);
	
	return htu21_status_ok;
}

/**
 * \brief Set I2C master mode.
 *        This determines whether the program will hold while ADC is accessed or will wait some time
 *
 * \param[in] htu21_i2c_master_mode : I2C mode
 *
 */
void htu21_set_i2c_master_mode(enum htu21_i2c_master_mode mode)
{
	i2c_master_mode = mode;
	return;
}

/**
 * \brief Writes the HTU21 8-bits command with the value passed
 *
 * \param[in] uint8_t : Command value to be written.
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_write_command( uint8_t cmd)
{
	enum status_code i2c_status;
	uint8_t data[1];
		
	data[0] = cmd;
		
	struct i2c_master_packet transfer = {
		.address     = HTU21_ADDR,
		.data_length = 1,
		.data        = data,
	};
	/* Do the transfer */
	i2c_status = i2c_master_write_packet_wait(&transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;
	
	return htu21_status_ok;
}

/**
 * \brief Writes the HTU21 8-bits command with the value passed
 *        Do not send the STOP bit in the I2C transfer
 *
 * \param[in] uint8_t : Command value to be written.
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_write_command_no_stop( uint8_t cmd)
{
	enum status_code i2c_status;
	uint8_t data[1];
		
	data[0] = cmd;
		
	struct i2c_master_packet transfer = {
		.address     = HTU21_ADDR,
		.data_length = 1,
		.data        = data,
	};
	
	/* Do the transfer */
	i2c_status = i2c_master_write_packet_wait_no_stop(&transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;
	
	return htu21_status_ok;
}

/**
 * \brief Check CRC
 *
 * \param[in] uint16_t : variable on which to check CRC
 * \param[in] uint8_t : CRC value
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : CRC check is OK
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_crc_check( uint16_t value, uint8_t crc)
{
	uint32_t polynom = 0x988000; // x^8 + x^5 + x^4 + 1
	uint32_t msb     = 0x800000;
	uint32_t mask    = 0xFF8000;
	uint32_t result  = (uint32_t)value<<8; // Pad with zeros as specified in spec
	
	while( msb != 0x80 ) {
		
		// Check if msb of current value is 1 and apply XOR mask
		if( result & msb )
			result = ((result ^ polynom) & mask) | ( result & ~mask);
			
		// Shift by one
		msb >>= 1;
		mask >>= 1;
		polynom >>=1;
	}
	if( result == crc )
		return 	htu21_status_ok;
	else
		return htu21_status_crc_error;
}

/**
 * \brief Reads the HTU21 user register.
 *
 * \param[out] uint8_t* : Storage of user register value
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_read_user_register(uint8_t *value)
{
	enum htu21_status status;
	enum status_code i2c_status;
	uint8_t buffer[1];
	buffer[0] = 0;

	/* Read data */
	struct i2c_master_packet read_transfer = {
		.address     = HTU21_ADDR,
		.data_length = 1,
		.data        = buffer,
	};
	
	// Send the Read Register Command
	status = htu21_write_command(HTU21_READ_USER_REG_COMMAND);
	if( status != htu21_status_ok )
		return status;
	
	i2c_status = i2c_master_read_packet_wait(&read_transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;

	*value = buffer[0];
	
	return htu21_status_ok;
}

/**
 * \brief Writes the htu21 user register with value
 *        Will read and keep the unreserved bits of the register
 *
 * \param[in] uint8_t : Register value to be set.
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_write_user_register(uint8_t value)
{
	enum htu21_status status;
	enum status_code i2c_status;
	uint8_t reg;
	uint8_t data[2];
	
	status = htu21_read_user_register(&reg);
	if( status != htu21_status_ok )
		return status;
	
	// Clear bits of reg that are not reserved
	reg &= HTU21_USER_REG_RESERVED_MASK;
	// Set bits from value that are not reserved
	reg |= (value & ~HTU21_USER_REG_RESERVED_MASK);
	
	data[0] = HTU21_WRITE_USER_REG_COMMAND;
	data[1] = reg;

	struct i2c_master_packet transfer = {
		.address     = HTU21_ADDR,
		.data_length = 2,
		.data        = data,
	};
	
	/* Do the transfer */
	i2c_status = i2c_master_write_packet_wait(&transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;
		
	return htu21_status_ok;
}

/**
 * \brief Reads the temperature ADC value
 *
 * \param[out] uint16_t* : Temperature ADC value.
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_temperature_conversion_and_read_adc( uint16_t *adc)
{
	enum htu21_status status = htu21_status_ok;
	enum status_code i2c_status;
	uint16_t _adc;
	uint8_t buffer[3];
	uint8_t crc;
	
	buffer[0] = 0;
	buffer[1] = 0;
	buffer[2] = 0;

	/* Read data */
    struct i2c_master_packet read_transfer = {
		.address     = HTU21_ADDR,
		.data_length = 3,
		.data        = buffer,
	};
	
	if( i2c_master_mode == htu21_i2c_hold) {
		status = htu21_write_command_no_stop(HTU21_READ_TEMPERATURE_W_HOLD_COMMAND);
	}
	else {
		status = htu21_write_command(HTU21_READ_TEMPERATURE_WO_HOLD_COMMAND);
		delay_ms(htu21_temperature_conversion_time/1000);
	}
	if( status != htu21_status_ok)
		return status;
		
    i2c_status = i2c_master_read_packet_wait(&read_transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;

	_adc = (buffer[0] << 8) | buffer[1];
	crc = buffer[2];
	
	// compute CRC
	status = htu21_crc_check(_adc,crc);
	if( status != htu21_status_ok)
		return status;
	
	*adc = _adc;

	return status;
}

/**
 * \brief Reads the relative humidity ADC value
 *
 * \param[out] uint16_t* : Relative humidity ADC value.
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_humidity_conversion_and_read_adc( uint16_t *adc)
{
	enum htu21_status status = htu21_status_ok;
	enum status_code i2c_status;
	uint16_t _adc;
	uint8_t buffer[3];
	uint8_t crc;
	
	buffer[0] = 0;
	buffer[1] = 0;
	buffer[2] = 0;

	/* Read data */
    struct i2c_master_packet read_transfer = {
		.address     = HTU21_ADDR,
		.data_length = 3,
		.data        = buffer,
	};
	
	if( i2c_master_mode == htu21_i2c_hold) {
		status = htu21_write_command_no_stop(HTU21_READ_HUMIDITY_W_HOLD_COMMAND);
	}
	else {
		status = htu21_write_command(HTU21_READ_HUMIDITY_WO_HOLD_COMMAND);
		delay_ms(htu21_humidity_conversion_time/1000);
	}
	if( status != htu21_status_ok)
		return status;
		
    i2c_status = i2c_master_read_packet_wait(&read_transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;

	_adc = (buffer[0] << 8) | buffer[1];
	crc = buffer[2];
	
	// compute CRC
	status = htu21_crc_check(_adc,crc);
	if( status != htu21_status_ok)
		return status;
	
	*adc = _adc;

	return status;
}

/**
 * \brief Reads the htu21 serial number.
 *
 * \param[out] uint64_t* : Serial number
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_read_serial_number(uint64_t * serial_number)
{
	enum htu21_status status;
	enum status_code i2c_status;
	uint8_t cmd_data[2];
	uint8_t rcv_data[14];
	uint8_t i;
		
	struct i2c_master_packet transfer = {
		.address     = HTU21_ADDR,
		.data_length = 2,
		.data        = cmd_data,
	};
	struct i2c_master_packet read_transfer = {
		.address     = HTU21_ADDR,
		.data_length = 8,
		.data        = rcv_data,
	};

	// Read the first 8 bytes
	cmd_data[0] = (HTU21_READ_SERIAL_FIRST_8BYTES_COMMAND>>8)&0xFF;
	cmd_data[1] = HTU21_READ_SERIAL_FIRST_8BYTES_COMMAND&0xFF;
		
	/* Do the transfer */
	i2c_master_write_packet_wait_no_stop(&transfer);
    i2c_status = i2c_master_read_packet_wait(&read_transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;

	// Read the last 6 bytes
	cmd_data[0] = (HTU21_READ_SERIAL_LAST_6BYTES_COMMAND>>8)&0xFF;
	cmd_data[1] = HTU21_READ_SERIAL_LAST_6BYTES_COMMAND&0xFF;
	
	read_transfer.data = &rcv_data[8];
	read_transfer.data_length = 6;

	/* Do the transfer */
	i2c_master_write_packet_wait_no_stop(&transfer);
	i2c_status = i2c_master_read_packet_wait(&read_transfer);
	if( i2c_status == STATUS_ERR_OVERFLOW )
		return htu21_status_no_i2c_acknowledge;
	if( i2c_status != STATUS_OK)
		return htu21_status_i2c_transfer_error;
	
	for( i=0 ; i<8 ; i+=2 ) {
		status = htu21_crc_check(rcv_data[i],rcv_data[i+1]);
		if( status != htu21_status_ok )
			return status;
	}
	for( i=8 ; i<14 ; i+=3 ) {
		status = htu21_crc_check( ((rcv_data[i]<<8)|(rcv_data[i+1])),rcv_data[i+2] );
		if( status != htu21_status_ok )
			return status;
	}
	
	*serial_number =	((uint64_t)rcv_data[0]<<56) | ((uint64_t)rcv_data[2]<<48) | ((uint64_t)rcv_data[4]<<40) | ((uint64_t)rcv_data[6]<<32)
					|	((uint64_t)rcv_data[8]<<24) | ((uint64_t)rcv_data[9]<<16) | ((uint64_t)rcv_data[11]<<8) | ((uint64_t)rcv_data[12]<<0);
	
	return htu21_status_ok;
	
}

/**
 * \brief Set temperature & humidity ADC resolution.
 *
 * \param[in] htu21_resolution : Resolution requested
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_set_resolution(enum htu21_resolution res)
{
	enum htu21_status status;
	uint8_t reg_value, tmp=0;
	uint32_t temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_14b_RH_12b;
	uint32_t humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_14b_RH_12b;
	
	if( res == htu21_resolution_t_14b_rh_12b) {
		tmp = HTU21_USER_REG_RESOLUTION_T_14b_RH_12b;
		temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_14b_RH_12b;
		humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_14b_RH_12b;
	}
	else if( res == htu21_resolution_t_13b_rh_10b) {
		tmp = HTU21_USER_REG_RESOLUTION_T_13b_RH_10b;
		temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_13b_RH_10b;
		humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_13b_RH_10b;
	}
	else if( res == htu21_resolution_t_12b_rh_8b) {
		tmp = HTU21_USER_REG_RESOLUTION_T_12b_RH_8b;
		temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_12b_RH_8b;
		humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_12b_RH_8b;
	}
	else if( res == htu21_resolution_t_11b_rh_11b) {
		tmp = HTU21_USER_REG_RESOLUTION_T_11b_RH_11b;
		temperature_conversion_time = HTU21_TEMPERATURE_CONVERSION_TIME_T_11b_RH_11b;
		humidity_conversion_time = HTU21_HUMIDITY_CONVERSION_TIME_T_11b_RH_11b;
	}
		
	status = htu21_read_user_register(&reg_value);
	if( status != htu21_status_ok )
		return status;
	
	// Clear the resolution bits
	reg_value &= ~HTU21_USER_REG_RESOLUTION_MASK;
	reg_value |= tmp & HTU21_USER_REG_RESOLUTION_MASK;
	
	htu21_temperature_conversion_time = temperature_conversion_time;
	htu21_humidity_conversion_time = humidity_conversion_time;
	
	status = htu21_write_user_register(reg_value);
	
	return status;
}

/**
 * \brief Provide battery status
 *
 * \param[out] htu21_battery_status* : Battery status
 *                      - htu21_battery_ok,
 *                      - htu21_battery_low
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_get_battery_status(enum htu21_battery_status *bat)
{
	enum htu21_status	status;
	uint8_t reg_value;

	status = htu21_read_user_register(&reg_value);
	if( status != htu21_status_ok)
		return status;

	if( reg_value & HTU21_USER_REG_END_OF_BATTERY_VDD_BELOW_2_25V )
		*bat = htu21_battery_low;
	else
		*bat = htu21_battery_ok;
		
	return status;
}

/**
 * \brief Enable heater
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_enable_heater(void)
{
	enum htu21_status status;
	uint8_t reg_value;
	
	status = htu21_read_user_register(&reg_value);
	if( status != htu21_status_ok )
		return status;
	
	// Clear the resolution bits
	reg_value |= HTU21_USER_REG_ONCHIP_HEATER_ENABLE;
	
	status = htu21_write_user_register(reg_value);

	return status;
}

/**
 * \brief Disable heater
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_disable_heater(void)
{
	enum htu21_status status;
	uint8_t reg_value;
	
	status = htu21_read_user_register(&reg_value);
	if( status != htu21_status_ok )
		return status;
	
	// Clear the resolution bits
	reg_value &= ~HTU21_USER_REG_ONCHIP_HEATER_ENABLE;
	
	status = htu21_write_user_register(reg_value);

	return status;
}

/**
 * \brief Get heater status
 *
 * \param[in] htu21_heater_status* : Return heater status (above or below 2.5V)
 *	                    - htu21_heater_off,
 *                      - htu21_heater_on
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 */
enum htu21_status htu21_get_heater_status(enum htu21_heater_status *heater)
{
	enum htu21_status status;
	uint8_t reg_value;
	
	status = htu21_read_user_register(&reg_value);
	if( status != htu21_status_ok )
		return status;
	
	// Get the heater enable bit in reg_value
	if( reg_value & HTU21_USER_REG_ONCHIP_HEATER_ENABLE)
		*heater = htu21_heater_on;
	else
		*heater = htu21_heater_off;
	
	return status;
}

/**
 * \brief Reads the relative humidity value.
 *
 * \param[out] float* : Celsius Degree temperature value
 * \param[out] float* : %RH Relative Humidity value
 *
 * \return htu21_status : status of HTU21
 *       - htu21_status_ok : I2C transfer completed successfully
 *       - htu21_status_i2c_transfer_error : Problem with i2c transfer
 *       - htu21_status_no_i2c_acknowledge : I2C did not acknowledge
 *       - htu21_status_crc_error : CRC check error
 */
enum htu21_status htu21_read_temperature_and_relative_humidity( float *temperature, float *humidity)
{
	enum htu21_status	status;
	uint16_t adc;
	
	status = htu21_temperature_conversion_and_read_adc( &adc);
	if( status != htu21_status_ok)
		return status;
	
	// Perform conversion function
	*temperature = (float)adc * TEMPERATURE_COEFF_MUL / (1UL<<16) + TEMPERATURE_COEFF_ADD;

	status = htu21_humidity_conversion_and_read_adc( &adc);
	if( status != htu21_status_ok)
		return status;
	
	// Perform conversion function
	*humidity = (float)adc * HUMIDITY_COEFF_MUL / (1UL<<16) + HUMIDITY_COEFF_ADD;
	
	return status;
}

/**
 * \brief Returns result of compensated humidity
 *
 * \param[in] float - Actual temperature measured (degC)
 * \param[in] float - Actual relative humidity measured (%RH)
 *
 * \return float - Compensated humidity (%RH).
 */
float htu21_compute_compensated_humidity(float temperature,float relative_humidity)
{
	return ( relative_humidity + (25 - temperature) * HTU21_TEMPERATURE_COEFFICIENT);
}

/**
 * \brief Returns the computed dew point
 *
 * \param[in] float - Actual temperature measured (degC)
 * \param[in] float - Actual relative humidity measured (%RH)
 *
 * \return float - Dew point temperature (DegC).
 */
float htu21_compute_dew_point(float temperature,float relative_humidity)
{
	double partial_pressure;
	double dew_point;
	
	// Missing power of 10
	partial_pressure = pow( 10, HTU21_CONSTANT_A - HTU21_CONSTANT_B / (temperature + HTU21_CONSTANT_C) );
	
	dew_point =  - HTU21_CONSTANT_B / (log10( relative_humidity * partial_pressure / 100) - HTU21_CONSTANT_A) - HTU21_CONSTANT_C;
	
	return (float)dew_point;
}

#ifdef __cplusplus
}
#endif