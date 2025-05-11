#include <stdio.h>
#include <string.h>

#include "circular_buffer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network.h"
#include "nvs_flash.h"
#include "p4nano_audio.h"
#include "vban.h"

static const char* TAG = "vban_demo";
static circular_buffer_t cb;

#define VBAN_LISTEN_PORT VBAN_DEFAULT_PORT	// Or the port specified by the sender
#define VBAN_EXPECTED_STREAM "TestStream1"	// Stream name to receive (empty string to receive any stream)
#define SPEAKER_VOLUME 60										// Volume level (0-100)
#define SAMPLE_RATE 48000										// Sample rate in Hz
#define BIT_DEPTH 16												// Bit depth
#define CHANNEL_COUNT 1											// Number of channels (1 for mono, 2 for stereo)
#define AUDIO_BUFFER_SIZE 32								// Buffer size for audio data in bytes
#define AUDIO_BUFFER_QUEUE_COUNT (VBAN_MAX_PAYLOAD_SIZE / AUDIO_BUFFER_SIZE + 2)	// Number of audio buffers in the queue

typedef struct {
	uint8_t* buffer;
	size_t size;
} audio_buffer_t;

static QueueHandle_t audio_queue;

static void vban_receive_callback(const vban_header_t* header, const uint8_t* audio_data, size_t audio_data_len, const char* sender_ip,
																	uint16_t sender_port, void* user_context) {
	uint32_t actual_sr = vban_get_sr_from_index((vban_sample_rate_index_t)(header->sr_subprotocol & VBAN_SR_INDEX_MASK));
	uint8_t num_channels = header->channels_m1 + 1;
	vban_data_type_t data_type = (vban_data_type_t)(header->format_codec & VBAN_DATATYPE_MASK);

	// Check if the received audio data matches the expected format
	if (actual_sr != SAMPLE_RATE) {
		ESP_LOGV(TAG, "Received sample rate %d does not match expected %d", (int)actual_sr, SAMPLE_RATE);
		return;
	}
	if (num_channels != CHANNEL_COUNT) {
		ESP_LOGV(TAG, "Received channel count %d does not match expected %d", (int)num_channels, CHANNEL_COUNT);
		return;
	}
	if (data_type != VBAN_DATATYPE_INT16) {
		ESP_LOGV(TAG, "Received data type %d does not match expected %d", data_type, VBAN_DATATYPE_INT16);
		return;
	}

	// Copy audio data to buffer
	int ret = circular_buffer_write(&cb, audio_data, audio_data_len);
	if (ret != CB_SUCCESS) {
		ESP_LOGE(TAG, "Failed to write to circular buffer: %d", ret);
		return;
	}

	// Send audio data to I2S writer task if the buffer has enough data
	while (circular_buffer_get_count(&cb) >= AUDIO_BUFFER_SIZE) {
		size_t readable_bytes = 0;
		void* readable_region = circular_buffer_get_readable_region(&cb, &readable_bytes);
		if (readable_region == NULL) {
			ESP_LOGE(TAG, "Failed to get readable region from circular buffer");
			return;
		}
		if (readable_bytes < AUDIO_BUFFER_SIZE) {
			// Should not happen, but just in case
			ESP_LOGE(TAG, "Not enough readable bytes in circular buffer: %zu", readable_bytes);
			return;
		}
		audio_buffer_t audio_buf;
		audio_buf.buffer = (uint8_t*)readable_region;
		audio_buf.size = AUDIO_BUFFER_SIZE;
		// Send audio buffer to I2S writer task
		if (xQueueSend(audio_queue, &audio_buf, portMAX_DELAY) != pdPASS) {
			ESP_LOGE(TAG, "Failed to send audio buffer to queue");
			return;
		}
		// Consume the data from the circular buffer
		ret = circular_buffer_consume(&cb, AUDIO_BUFFER_SIZE);
		if (ret != CB_SUCCESS) {
			ESP_LOGE(TAG, "Failed to consume data from circular buffer: %d", ret);
			return;
		}
	}
}

static void i2s_writer(void* args) {
	audio_buffer_t audio_buf;
	i2s_chan_handle_t tx_handle = NULL;
	bsp_audio_get_i2s_handle(&tx_handle, NULL);
	if (!tx_handle) {
		ESP_LOGE(TAG, "[writer] Failed to get I2S handle");
		abort();
	}

	while (1) {
		if (xQueueReceive(audio_queue, &audio_buf, portMAX_DELAY) == pdPASS) {
			size_t bytes_written = 0;
			esp_err_t ret = i2s_channel_write(tx_handle, audio_buf.buffer, audio_buf.size, &bytes_written, portMAX_DELAY);
			if (ret != ESP_OK) {
				ESP_LOGE(TAG, "[writer] i2s write failed");
				abort();
			}
			if (bytes_written != audio_buf.size) {
				ESP_LOGW(TAG, "[writer] %d bytes should be written but only %d bytes are written", audio_buf.size, bytes_written);
			}
		}
	}
}

void app_main(void) {
	esp_err_t ret = ESP_OK;

	// --- Initialize circular buffer ---
	ret = circular_buffer_init(&cb, VBAN_MAX_PAYLOAD_SIZE * 2);
	if (ret != CB_SUCCESS) {
		ESP_LOGE(TAG, "Failed to initialize circular buffer");
		abort();
	}

	// --- Initialize audio ---

	// I2S initialization
	i2s_std_config_t i2s_config = bsp_get_i2s_duplex_config(SAMPLE_RATE, BIT_DEPTH, CHANNEL_COUNT);
	ret = bsp_audio_init(&i2s_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to initialize I2S");
		abort();
	}

	// Initialize audio device
	esp_codec_dev_handle_t speaker_handle = bsp_audio_codec_speaker_init();
	if (!speaker_handle) {
		ESP_LOGE(TAG, "Failed to initialize speaker codec");
		abort();
	}
	// Set volume
	ret = esp_codec_dev_set_out_vol(speaker_handle, SPEAKER_VOLUME);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "Failed to set speaker volume");
	}
	// Open device
	esp_codec_dev_sample_info_t fs = {
			.sample_rate = SAMPLE_RATE,
			.bits_per_sample = BIT_DEPTH,
			.channel = CHANNEL_COUNT,
	};
	ret = esp_codec_dev_open(speaker_handle, &fs);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "Failed to open speaker codec");
		abort();
	}

	// Create FreeRTOS queue
	audio_queue = xQueueCreate(AUDIO_BUFFER_QUEUE_COUNT, sizeof(audio_buffer_t));
	if (!audio_queue) {
		ESP_LOGE(TAG, "Failed to create audio queue");
		abort();
	}

	xTaskCreate(i2s_writer, "i2s_writer", 4096, NULL, 5, NULL);

	// --- Initialize network ---

	ESP_ERROR_CHECK(nvs_flash_init());

	network_config_t net_config;
	ret = network_create_dhcp_config(&net_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create DHCP config: %s", esp_err_to_name(ret));
		return;
	}

	ret = network_config_mdns(&net_config, "esp32-p4-nano", NULL);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to configure mDNS: %s", esp_err_to_name(ret));
		return;
	}

	ret = network_init(&net_config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(ret));
		return;
	}

	// --- Initialize VBAN ---

	vban_receiver_config_t receiver_cfg = {0};
	strncpy(receiver_cfg.expected_stream_name, VBAN_EXPECTED_STREAM, VBAN_STREAM_NAME_MAX_LEN - 1);
	receiver_cfg.listen_port = VBAN_LISTEN_PORT;
	receiver_cfg.audio_callback = vban_receive_callback;

	// Set parameters for the receiving task (unnecessary if using default values in vban.c)
	receiver_cfg.core_id = tskNO_AFFINITY;	// Run on any core
	receiver_cfg.task_priority = 5;
	receiver_cfg.task_stack_size = 4096;

	vban_handle_t receiver_handle = vban_receiver_create(&receiver_cfg);
	if (!receiver_handle) {
		ESP_LOGE(TAG, "Failed to create VBAN receiver");
		vTaskDelete(NULL);
		return;
	}

	esp_err_t err = vban_receiver_start(receiver_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start VBAN receiver: %s", esp_err_to_name(err));
		vban_receiver_delete(receiver_handle);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "VBAN Receiver initialized and started. Listening for stream '%s' on port %d.", VBAN_EXPECTED_STREAM, VBAN_LISTEN_PORT);
}