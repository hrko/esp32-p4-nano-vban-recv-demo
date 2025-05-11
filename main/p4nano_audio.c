// SPDX-License-Identifier: Apache-2.0
/*
 * Based on the BSP code available at:
 * https://github.com/waveshareteam/Waveshare-ESP32-components
 */
#include "p4nano_audio.h"  // Include the header file for this module

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"  // For ES8311 default address and interfaces
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "p4nano_audio";

// I2C related variables
static bool i2c_initialized = false;
static i2c_master_bus_handle_t i2c_handle = NULL;

// I2S related variables
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static const audio_codec_data_if_t *i2s_data_if = NULL; /* Codec data interface */

i2s_std_config_t bsp_get_i2s_duplex_config(uint32_t sample_rate, uint8_t bit_depth, uint8_t channels) {
  if (channels < 1 || channels > 2) {
    ESP_LOGE(TAG, "Invalid number of channels: %d, using mono as default", channels);
    channels = 1;  // Default to mono if invalid
  }

  i2s_slot_mode_t slot_mode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
  i2s_data_bit_width_t data_bit_width;

  switch (bit_depth) {
    case 8:
      data_bit_width = I2S_DATA_BIT_WIDTH_8BIT;
      break;
    case 16:
      data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
      break;
    case 24:
      data_bit_width = I2S_DATA_BIT_WIDTH_24BIT;
      break;
    case 32:
      data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
      break;
    default:
      ESP_LOGE(TAG, "Unsupported bit depth: %d, using 16-bit as default", bit_depth);
      data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;  // Default to 16-bit
  }

  return (i2s_std_config_t){
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
      .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(data_bit_width, slot_mode),
      .gpio_cfg =
          (i2s_std_gpio_config_t){
              .mclk = BSP_I2S_MCLK,
              .bclk = BSP_I2S_SCLK,
              .ws = BSP_I2S_LCLK,
              .dout = BSP_I2S_DOUT,
              .din = BSP_I2S_DSIN,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
}

esp_err_t bsp_i2c_init(void) {
  /* I2C was initialized before */
  if (i2c_initialized) {
    return ESP_OK;
  }

  i2c_master_bus_config_t i2c_bus_conf = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .sda_io_num = BSP_I2C_SDA,
      .scl_io_num = BSP_I2C_SCL,
      .i2c_port = CONFIG_BSP_I2C_NUM,
      // Set I2C clock speed, can be adjusted via menuconfig CONFIG_BSP_I2C_CLK_SPEED_HZ
      .glitch_ignore_cnt = 7,                // Default value
      .flags.enable_internal_pullup = true,  // Enable internal pull-up resistors
  };

  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle), TAG, "I2C new master bus failed");

  i2c_initialized = true;
  ESP_LOGI(TAG, "I2C initialized successfully");

  return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void) {
  if (!i2c_initialized) {
    ESP_LOGW(TAG, "I2C already de-initialized");
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(i2c_del_master_bus(i2c_handle), TAG, "I2C delete master bus failed");
  i2c_handle = NULL;
  i2c_initialized = false;
  ESP_LOGI(TAG, "I2C de-initialized successfully");
  return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void) {
  if (!i2c_initialized) {
    ESP_LOGE(TAG, "I2C is not initialized yet");
    return NULL;
  }
  return i2c_handle;
}

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config) {
  if (i2s_tx_chan || i2s_rx_chan) {
    ESP_LOGW(TAG, "Audio has been initialized already");
    return ESP_OK;
  }

  /* Setup I2S peripheral */
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;  // Auto clear the legacy data in the DMA buffer
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan), TAG, "I2S new channel failed");

  /* Setup I2S channels */
  const i2s_std_config_t std_cfg_default = bsp_get_i2s_duplex_config(22050, 16, 1);  // Default to 22.05 kHz, 16-bit, mono
  const i2s_std_config_t *p_i2s_cfg = (i2s_config != NULL) ? i2s_config : &std_cfg_default;

  esp_err_t ret = ESP_OK;
  if (i2s_tx_chan != NULL) {
    ret = i2s_channel_init_std_mode(i2s_tx_chan, p_i2s_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize I2S Tx channel: %s", esp_err_to_name(ret));
      goto err_init_tx;
    }
    ret = i2s_channel_enable(i2s_tx_chan);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to enable I2S Tx channel: %s", esp_err_to_name(ret));
      goto err_enable_tx;
    }
  }

  if (i2s_rx_chan != NULL) {
    ret = i2s_channel_init_std_mode(i2s_rx_chan, p_i2s_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize I2S Rx channel: %s", esp_err_to_name(ret));
      goto err_init_rx;
    }
    ret = i2s_channel_enable(i2s_rx_chan);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to enable I2S Rx channel: %s", esp_err_to_name(ret));
      goto err_enable_rx;
    }
  }

  /* Create I2S data interface for codec */
  audio_codec_i2s_cfg_t i2s_data_cfg = {
      .port = CONFIG_BSP_I2S_NUM,
      .tx_handle = i2s_tx_chan,
      .rx_handle = i2s_rx_chan,
  };
  i2s_data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
  ESP_RETURN_ON_FALSE(i2s_data_if, ESP_FAIL, TAG, "Failed to create I2S data interface");

  ESP_LOGI(TAG, "Audio I2S initialized successfully (Tx:%p, Rx:%p)", i2s_tx_chan, i2s_rx_chan);
  return ESP_OK;

// Error handling labels
err_enable_rx:
  if (i2s_rx_chan) i2s_channel_disable(i2s_rx_chan);
err_init_rx:
  // No cleanup needed for channel initialization errors
err_enable_tx:
  if (i2s_tx_chan) i2s_channel_disable(i2s_tx_chan);
err_init_tx:
  // No cleanup needed for channel initialization errors
  if (i2s_tx_chan || i2s_rx_chan) i2s_del_channel(i2s_tx_chan);  // Assuming tx/rx use same channel handle internally
  i2s_tx_chan = NULL;
  i2s_rx_chan = NULL;
  ESP_LOGE(TAG, "Failed to initialize I2S channels: %s", esp_err_to_name(ret));
  return ret;
}

// Helper function to initialize common codec components
static esp_err_t bsp_codec_init_interfaces(const audio_codec_ctrl_if_t **ctrl_if, const audio_codec_gpio_if_t **gpio_if) {
  esp_err_t ret;
  // Initialize I2C if not already done
  ret = bsp_i2c_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize I2C for codec");
    return ret;
  }
  ESP_RETURN_ON_FALSE(i2c_handle, ESP_FAIL, TAG, "I2C handle is NULL after init");

  // Initialize I2S and data interface if not already done
  if (i2s_data_if == NULL) {
    ret = bsp_audio_init(NULL);  // Use default I2S settings
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize I2S for codec");
      return ret;
    }
    ESP_RETURN_ON_FALSE(i2s_data_if, ESP_FAIL, TAG, "I2S data interface is NULL after init");
  }

  // Create I2C control interface for ES8311
  audio_codec_i2c_cfg_t i2c_cfg = {
      .port = CONFIG_BSP_I2C_NUM,
      .addr = ES8311_CODEC_DEFAULT_ADDR,
      .bus_handle = i2c_handle,
  };
  *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
  ESP_RETURN_ON_FALSE(*ctrl_if, ESP_FAIL, TAG, "Failed to create I2C control interface");

  // Create GPIO interface (currently unused by ES8311 driver in this config)
  *gpio_if = audio_codec_new_gpio();
  ESP_RETURN_ON_FALSE(*gpio_if, ESP_FAIL, TAG, "Failed to create GPIO interface");

  return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) {
  const audio_codec_ctrl_if_t *i2c_ctrl_if = NULL;
  const audio_codec_gpio_if_t *gpio_if = NULL;

  esp_err_t ret = bsp_codec_init_interfaces(&i2c_ctrl_if, &gpio_if);
  if (ret != ESP_OK) {
    return NULL;
  }

  esp_codec_dev_hw_gain_t gain = {
      .pa_voltage = 5.0,         // Typical PA voltage
      .codec_dac_voltage = 3.3,  // Typical codec DAC voltage
  };

  es8311_codec_cfg_t es8311_cfg = {
      .ctrl_if = i2c_ctrl_if,
      .gpio_if = gpio_if,
      .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,  // Speaker is DAC only
      .pa_pin = BSP_POWER_AMP_IO,
      .pa_reverted = false,
      .master_mode = false,  // ESP32 is I2S master
      .use_mclk = true,      // Use MCLK from ESP32
      .digital_mic = false,  // Analog microphone input
      .invert_mclk = false,
      .invert_sclk = false,
      .hw_gain = gain,
  };
  const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
  ESP_RETURN_ON_FALSE(es8311_dev, NULL, TAG, "Failed to create ES8311 codec interface");

  esp_codec_dev_cfg_t codec_dev_cfg = {
      .dev_type = ESP_CODEC_DEV_TYPE_OUT,  // Speaker is output device
      .codec_if = es8311_dev,
      .data_if = i2s_data_if,
  };
  esp_codec_dev_handle_t speaker_handle = esp_codec_dev_new(&codec_dev_cfg);
  ESP_RETURN_ON_FALSE(speaker_handle, NULL, TAG, "Failed to create speaker codec device");

  ESP_LOGI(TAG, "Speaker codec initialized successfully");
  return speaker_handle;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void) {
  const audio_codec_ctrl_if_t *i2c_ctrl_if = NULL;
  const audio_codec_gpio_if_t *gpio_if = NULL;

  esp_err_t ret = bsp_codec_init_interfaces(&i2c_ctrl_if, &gpio_if);
  if (ret != ESP_OK) {
    return NULL;
  }

  esp_codec_dev_hw_gain_t gain = {
      .pa_voltage = 5.0,         // Not directly relevant for mic, but set for consistency
      .codec_dac_voltage = 3.3,  // Not directly relevant for mic, but set for consistency
  };

  es8311_codec_cfg_t es8311_cfg = {
      .ctrl_if = i2c_ctrl_if,
      .gpio_if = gpio_if,
      .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,  // Microphone is ADC only (or BOTH if needed later)
      .pa_pin = BSP_POWER_AMP_IO,                 // PA pin may not be needed for mic only, but configured
      .pa_reverted = false,
      .master_mode = false,  // ESP32 is I2S master
      .use_mclk = true,      // Use MCLK from ESP32
      .digital_mic = false,  // Analog microphone input
      .invert_mclk = false,
      .invert_sclk = false,
      .hw_gain = gain,  // Gain settings might need adjustment for mic
  };
  const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
  ESP_RETURN_ON_FALSE(es8311_dev, NULL, TAG, "Failed to create ES8311 codec interface for mic");

  esp_codec_dev_cfg_t codec_dev_cfg = {
      .dev_type = ESP_CODEC_DEV_TYPE_IN,  // Microphone is input device
      .codec_if = es8311_dev,
      .data_if = i2s_data_if,
  };
  esp_codec_dev_handle_t mic_handle = esp_codec_dev_new(&codec_dev_cfg);
  ESP_RETURN_ON_FALSE(mic_handle, NULL, TAG, "Failed to create microphone codec device");

  ESP_LOGI(TAG, "Microphone codec initialized successfully");
  return mic_handle;
}

void bsp_audio_get_i2s_handle(i2s_chan_handle_t *tx, i2s_chan_handle_t *rx) {
  if (tx) *tx = i2s_tx_chan;
  if (rx) *rx = i2s_rx_chan;
}