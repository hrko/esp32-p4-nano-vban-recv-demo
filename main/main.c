#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "nvs_flash.h"
#include "vban.h"

static const char* TAG = "vban_demo";

#define VBAN_LISTEN_PORT VBAN_DEFAULT_PORT	// または送信側が指定するポート
#define VBAN_EXPECTED_STREAM "TestStream1"	// 受信したいストリーム名 (空文字列で任意のストリームを受信)

static void vban_receive_callback(const vban_header_t* header, const uint8_t* audio_data, size_t audio_data_len, const char* sender_ip,
																	uint16_t sender_port, void* user_context) {
	// (user_context を必要に応じてキャストして使用)
	// Example: int my_value = *(int*)user_context;

	uint32_t actual_sr = vban_get_sr_from_index((vban_sample_rate_index_t)(header->sr_subprotocol & VBAN_SR_INDEX_MASK));
	uint8_t num_samples = header->samples_per_frame_m1 + 1;
	uint8_t num_channels = header->channels_m1 + 1;
	vban_data_type_t data_type = (vban_data_type_t)(header->format_codec & VBAN_DATATYPE_MASK);
	size_t sample_size = vban_get_data_type_size(data_type);

	ESP_LOGI(TAG, "Received VBAN Audio from %s:%u, Stream: '%.*s'", sender_ip, (unsigned)sender_port, VBAN_STREAM_NAME_MAX_LEN,
					 header->stream_name);
	ESP_LOGI(TAG, "  SR: %u Hz, Samples: %u, Channels: %u, Format: %d, DataLen: %u bytes, Frame: %u", (unsigned)actual_sr,
					 (unsigned)num_samples, (unsigned)num_channels, data_type, (unsigned)audio_data_len, (unsigned)header->frame_counter);

	// ここでオーディオデータを処理 (例: DACに出力、ファイルに保存など)
	// For example, print first few sample values if INT16 stereo
	if (data_type == VBAN_DATATYPE_INT16 && num_channels == 2 && audio_data_len >= 4) {
		// int16_t* pcm_data = (int16_t*)audio_data;
		// ESP_LOGI(TAG, "  Sample L0: %d, R0: %d", pcm_data[0], pcm_data[1]);
	}
}

void app_main(void) {
	// --- Initialize network ---

	ESP_ERROR_CHECK(nvs_flash_init());

	esp_err_t ret = ESP_OK;

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
	// receiver_cfg.user_context = &some_user_data; // 必要であればユーザーデータを渡す

	// 受信タスクのパラメータ設定 (vban.cのデフォルト値でよければ設定不要)
	receiver_cfg.core_id = -1;	// 任意のコアで実行
	receiver_cfg.task_priority = 5;
	receiver_cfg.task_stack_size = 4096 * 2;	// コールバック内でログ出力など多めにする場合はスタックを増やす

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