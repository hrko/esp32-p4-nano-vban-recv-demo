// SPDX-License-Identifier: Apache-2.0
/*
 * Based on the BSP code available at:
 * https://github.com/waveshareteam/Waveshare-ESP32-components
 */
#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 * ESP32-P4-NANO - I2C and Audio Pinout
 **************************************************************************************************/
/* I2C */
#define BSP_I2C_SCL (GPIO_NUM_8)
#define BSP_I2C_SDA (GPIO_NUM_7)

/* Audio */
#define BSP_I2S_SCLK (GPIO_NUM_12)
#define BSP_I2S_MCLK (GPIO_NUM_13)
#define BSP_I2S_LCLK (GPIO_NUM_10)
#define BSP_I2S_DOUT (GPIO_NUM_9)
#define BSP_I2S_DSIN (GPIO_NUM_11)
#define BSP_POWER_AMP_IO (GPIO_NUM_53)

/**************************************************************************************************
 * Configuration for I2C and I2S interfaces
 **************************************************************************************************/
/* I2C peripheral index -> [0,1]   ESP32P4 has two I2C peripherals, pick the one you want to use. */
#define CONFIG_BSP_I2C_NUM 0
/* I2S peripheral index -> [0,1,2] ESP32P4 has three I2S peripherals, pick the one you want to use. */
#define CONFIG_BSP_I2S_NUM 0

/**
 * @brief Init I2C driver
 *
 * @return
 * - ESP_OK                On success
 * - ESP_ERR_INVALID_ARG   I2C parameter error
 * - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 * - ESP_OK                On success
 * - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Get I2C driver handle
 *
 * @return
 * - I2C handle or NULL if not initialized
 *
 */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

/**************************************************************************************************
 *
 * I2S audio interface
 *
 **************************************************************************************************/

/**
 * @brief Init audio
 *
 * @note There is no deinit audio function. Users can free audio resources by calling i2s_del_channel()
 * @param[in]  i2s_config I2S configuration. Pass NULL to use default values (Mono, duplex, 16bit, 22050 Hz)
 * @return
 * - ESP_OK                On success
 * - ESP_ERR_NOT_SUPPORTED The communication mode is not supported on the current chip
 * - ESP_ERR_INVALID_ARG   NULL pointer or invalid configuration
 * - ESP_ERR_NOT_FOUND     No available I2S channel found
 * - ESP_ERR_NO_MEM        No memory for storing the channel information
 * - ESP_ERR_INVALID_STATE This channel has not initialized or already started
 */
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);

/**
 * @brief Get I2S configuration for duplex mode
 *
 * @param[in] sample_rate Sample rate for the I2S configuration
 * @param[in] bit_depth   Bit depth for the I2S configuration (e.g., 8, 16, 24, 32)
 * @param[in] channels    Number of channels (1 for mono, 2 for stereo)
 * @return Configured I2S standard configuration
 */
i2s_std_config_t bsp_get_i2s_duplex_config(uint32_t sample_rate, uint8_t bit_depth, uint8_t channels);

/**
 * @brief Initialize speaker codec device
 *
 * @note This function will call bsp_i2c_init() and bsp_audio_init() internally if they haven't been called yet.
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

/**
 * @brief Initialize microphone codec device
 *
 * @note This function will call bsp_i2c_init() and bsp_audio_init() internally if they haven't been called yet.
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

/**
 * @brief Get I2S channel handle
 *
 * @param[in]  tx pointer to I2S transmit channel handle, can be NULL
 * @param[in]  rx pointer to I2S receive channel handle, can be NULL
 */
void bsp_audio_get_i2s_handle(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx);

#ifdef __cplusplus
}
#endif