#include "vban.h"

#include <string.h>	 // For memcpy, strlen, strncmp

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"		 // For gethostbyname (not used here for simplicity, direct IP)
#include "lwip/sockets.h"	 // For socket functions

static const char* TAG = "VBAN";

// For converting sample rate index to actual SR value
static const uint32_t VBAN_SAMPLE_RATES_LUT[VBAN_SR_MAX_INDEX] = {
		6000,		12000,	24000,	48000, 96000, 192000, 384000, 8000,		16000,	32000, 64000,
		128000, 256000, 512000, 11025, 22050, 44100,	88200,	176400, 352800, 705600
		// Undefined SRs are not in this LUT for direct indexing
};

// Internal VBAN instance structure
typedef enum { VBAN_INSTANCE_TYPE_SENDER, VBAN_INSTANCE_TYPE_RECEIVER } vban_instance_type_t;

typedef enum { VBAN_RECEIVER_STATE_IDLE, VBAN_RECEIVER_STATE_RUNNING, VBAN_RECEIVER_STATE_STOPPING } vban_receiver_state_t;

struct vban_instance_s {
	vban_instance_type_t type;
	int sock_fd;
	union {
		struct {
			vban_sender_config_t config;
			uint32_t frame_counter;
			struct sockaddr_in dest_addr;
		} sender;
		struct {
			vban_receiver_config_t config;
			TaskHandle_t receive_task_handle;
			volatile vban_receiver_state_t state;
		} receiver;
	} ctx;
};

// --- Utility Function Implementations ---

size_t vban_get_data_type_size(vban_data_type_t data_type) {
	switch (data_type) {
		case VBAN_DATATYPE_UINT8:
			return 1;
		case VBAN_DATATYPE_INT16:
			return 2;
		case VBAN_DATATYPE_INT24:
			return 3;
		case VBAN_DATATYPE_INT32:
			return 4;
		case VBAN_DATATYPE_FLOAT32:
			return 4;
		case VBAN_DATATYPE_FLOAT64:
			return 8;
		// VBAN_DATATYPE_INT12 and VBAN_DATATYPE_INT10 are more complex (not simple byte multiples)
		// For simplicity, we'll assume they are not used for now or require packing.
		default:
			return 0;
	}
}

uint32_t vban_get_sr_from_index(vban_sample_rate_index_t sr_idx) {
	if (sr_idx < VBAN_SR_MAX_INDEX && sr_idx <= VBAN_SR_705600) {	 // Check against highest defined valid index
		return VBAN_SAMPLE_RATES_LUT[sr_idx];
	}
	return 0;
}

vban_sample_rate_index_t vban_get_index_from_sr(uint32_t sample_rate) {
	for (int i = 0; i <= VBAN_SR_705600; ++i) {
		if (VBAN_SAMPLE_RATES_LUT[i] == sample_rate) {
			return (vban_sample_rate_index_t)i;
		}
	}
	return VBAN_SR_MAX_INDEX;	 // Not found
}

static uint8_t vban_util_get_sr_subprotocol_byte(vban_sample_rate_index_t sr_idx, uint8_t sub_protocol_id) {
	return (uint8_t)((sr_idx & VBAN_SR_INDEX_MASK) | ((sub_protocol_id << VBAN_SUBPROTOCOL_SHIFT) & VBAN_SUBPROTOCOL_MASK));
}

static void vban_util_parse_sr_subprotocol_byte(uint8_t byte_val, vban_sample_rate_index_t* sr_idx, uint8_t* sub_protocol_id) {
	if (sr_idx) *sr_idx = (vban_sample_rate_index_t)(byte_val & VBAN_SR_INDEX_MASK);
	if (sub_protocol_id) *sub_protocol_id = (uint8_t)((byte_val & VBAN_SUBPROTOCOL_MASK) >> VBAN_SUBPROTOCOL_SHIFT);
}

static uint8_t vban_util_get_format_codec_byte(vban_data_type_t data_type, uint8_t codec_id, bool reserved_bit_val) {
	// Per spec, reserved bit (bit 3) must be 0 for PCM.
	uint8_t reserved_bit = reserved_bit_val ? VBAN_RESERVED_BIT_MASK : 0x00;
	return (uint8_t)((data_type & VBAN_DATATYPE_MASK) | reserved_bit | ((codec_id << VBAN_CODEC_SHIFT) & VBAN_CODEC_MASK));
}

static void vban_util_parse_format_codec_byte(uint8_t byte_val, vban_data_type_t* data_type, uint8_t* codec_id, bool* reserved_bit_val) {
	if (data_type) *data_type = (vban_data_type_t)(byte_val & VBAN_DATATYPE_MASK);
	if (reserved_bit_val) *reserved_bit_val = (byte_val & VBAN_RESERVED_BIT_MASK) != 0;
	if (codec_id) *codec_id = (uint8_t)((byte_val & VBAN_CODEC_MASK) >> VBAN_CODEC_SHIFT);
}

// --- Sender Implementation ---

vban_handle_t vban_sender_create(const vban_sender_config_t* config) {
	if (!config || !config->dest_ip[0] || strlen(config->stream_name) >= VBAN_STREAM_NAME_MAX_LEN) {
		ESP_LOGE(TAG, "Sender create: Invalid arguments");
		return NULL;
	}

	vban_handle_t handle = (vban_handle_t)calloc(1, sizeof(struct vban_instance_s));
	if (!handle) {
		ESP_LOGE(TAG, "Sender create: No memory for handle");
		return NULL;
	}

	handle->type = VBAN_INSTANCE_TYPE_SENDER;
	memcpy(&handle->ctx.sender.config, config, sizeof(vban_sender_config_t));
	handle->ctx.sender.frame_counter = 0;

	handle->sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (handle->sock_fd < 0) {
		ESP_LOGE(TAG, "Sender create: Failed to create socket: %s", strerror(errno));
		free(handle);
		return NULL;
	}

	memset(&handle->ctx.sender.dest_addr, 0, sizeof(handle->ctx.sender.dest_addr));
	handle->ctx.sender.dest_addr.sin_family = AF_INET;
	handle->ctx.sender.dest_addr.sin_port = htons(config->dest_port > 0 ? config->dest_port : VBAN_DEFAULT_PORT);
	if (inet_pton(AF_INET, config->dest_ip, &handle->ctx.sender.dest_addr.sin_addr) <= 0) {
		ESP_LOGE(TAG, "Sender create: Invalid destination IP address %s", config->dest_ip);
		close(handle->sock_fd);
		free(handle);
		return NULL;
	}

	ESP_LOGI(TAG, "VBAN Sender created for stream '%s' to %s:%d", config->stream_name, config->dest_ip,
					 ntohs(handle->ctx.sender.dest_addr.sin_port));
	return handle;
}

esp_err_t vban_sender_delete(vban_handle_t handle) {
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_SENDER) {
		return ESP_ERR_VBAN_INVALID_HANDLE;
	}

	if (handle->sock_fd >= 0) {
		close(handle->sock_fd);
		handle->sock_fd = -1;
	}
	ESP_LOGI(TAG, "VBAN Sender for stream '%s' deleted", handle->ctx.sender.config.stream_name);
	free(handle);
	return ESP_OK;
}

esp_err_t vban_audio_send(vban_handle_t handle, const void* audio_data, uint8_t num_samples /* 1-256 */) {
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_SENDER) {
		return ESP_ERR_VBAN_INVALID_HANDLE;
	}
	if (!audio_data || num_samples == 0) {
		return ESP_ERR_VBAN_INVALID_ARG;
	}

	const vban_audio_format_t* fmt = &handle->ctx.sender.config.audio_format;
	size_t sample_component_size = vban_get_data_type_size(fmt->data_type);
	if (sample_component_size == 0) {
		ESP_LOGE(TAG, "Audio send: Invalid data type");
		return ESP_ERR_VBAN_INVALID_ARG;
	}

	size_t audio_payload_size = (size_t)num_samples * fmt->num_channels * sample_component_size;
	if (audio_payload_size > VBAN_MAX_PAYLOAD_SIZE) {
		ESP_LOGE(TAG, "Audio send: Payload size %d exceeds max %d", audio_payload_size, VBAN_MAX_PAYLOAD_SIZE);
		return ESP_ERR_VBAN_PAYLOAD_TOO_LARGE;
	}

	uint8_t packet_buffer[VBAN_MAX_PACKET_SIZE];
	vban_header_t* header = (vban_header_t*)packet_buffer;

	header->vban_magic = VBAN_MAGIC_NUMBER;
	header->sr_subprotocol = vban_util_get_sr_subprotocol_byte(fmt->sample_rate_idx, VBAN_SUBPROTOCOL_AUDIO);
	header->samples_per_frame_m1 = num_samples - 1;
	header->channels_m1 = fmt->num_channels - 1;
	// For PCM audio, codec is VBAN_CODEC_PCM (0), reserved bit is 0.
	header->format_codec = vban_util_get_format_codec_byte(fmt->data_type, VBAN_CODEC_PCM, false);
	strncpy(header->stream_name, handle->ctx.sender.config.stream_name, VBAN_STREAM_NAME_MAX_LEN);
	// Ensure null termination if name is shorter
	if (strlen(handle->ctx.sender.config.stream_name) < VBAN_STREAM_NAME_MAX_LEN) {
		header->stream_name[strlen(handle->ctx.sender.config.stream_name)] = '\0';
	} else {
		// No null termination if name is exactly 16 chars, as per some interpretations of spec.
		// However, it's safer to ensure it's treated as a C string for debugging.
		// Let's assume the receiver handles non-null-terminated names if they are full length.
	}
	header->frame_counter = handle->ctx.sender.frame_counter++;

	memcpy(packet_buffer + VBAN_HEADER_SIZE, audio_data, audio_payload_size);

	ssize_t sent_len = sendto(handle->sock_fd, packet_buffer, VBAN_HEADER_SIZE + audio_payload_size, 0,
														(struct sockaddr*)&handle->ctx.sender.dest_addr, sizeof(handle->ctx.sender.dest_addr));

	if (sent_len < 0) {
		ESP_LOGE(TAG, "Audio send: sendto failed: %s", strerror(errno));
		return ESP_ERR_VBAN_SEND_FAIL;
	}
	if (sent_len != (VBAN_HEADER_SIZE + audio_payload_size)) {
		ESP_LOGW(TAG, "Audio send: Partial send. Expected %d, sent %d", (int)(VBAN_HEADER_SIZE + audio_payload_size), (int)sent_len);
		return ESP_ERR_VBAN_SEND_FAIL;	// Or a more specific error
	}
	// ESP_LOGD(TAG, "Sent VBAN audio packet, %d bytes, frame %u", sent_len, header->frame_counter -1);
	return ESP_OK;
}

// --- Receiver Implementation ---

static void vban_receive_task(void* pvParameters) {
	vban_handle_t handle = (vban_handle_t)pvParameters;
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_RECEIVER) {
		ESP_LOGE(TAG, "Receive task: Invalid handle passed");
		vTaskDelete(NULL);
		return;
	}

	uint8_t rx_buffer[VBAN_MAX_PACKET_SIZE];
	struct sockaddr_in source_addr;
	socklen_t socklen = sizeof(source_addr);

	ESP_LOGI(TAG, "VBAN Receiver task started for stream '%s' on port %d",
					 handle->ctx.receiver.config.expected_stream_name[0] ? handle->ctx.receiver.config.expected_stream_name : "<ANY>",
					 handle->ctx.receiver.config.listen_port);

	handle->ctx.receiver.state = VBAN_RECEIVER_STATE_RUNNING;

	while (handle->ctx.receiver.state == VBAN_RECEIVER_STATE_RUNNING) {
		ssize_t len = recvfrom(handle->sock_fd, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr*)&source_addr, &socklen);

		if (len < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {	// Non-blocking socket would return this
				vTaskDelay(pdMS_TO_TICKS(10));								// Avoid busy loop if socket is non-blocking (not set here)
				continue;
			}
			if (handle->ctx.receiver.state != VBAN_RECEIVER_STATE_RUNNING) {	// Socket closed during stop
				break;
			}
			ESP_LOGE(TAG, "Receive task: recvfrom failed: %s", strerror(errno));
			vTaskDelay(pdMS_TO_TICKS(100));	 // Wait a bit before retrying on error
			continue;
		}

		if (len < VBAN_HEADER_SIZE) {
			ESP_LOGD(TAG, "Receive task: Packet too short (%d bytes)", (int)len);
			continue;
		}

		vban_header_t* header = (vban_header_t*)rx_buffer;

		if (header->vban_magic != VBAN_MAGIC_NUMBER) {
			ESP_LOGD(TAG, "Receive task: Invalid VBAN magic number 0x%08X", (unsigned int)header->vban_magic);
			continue;
		}

		// Optional: Filter by stream name
		if (handle->ctx.receiver.config.expected_stream_name[0] != '\0') {
			// Null-terminate received name for safe comparison if it's shorter than max
			char received_stream_name[VBAN_STREAM_NAME_MAX_LEN + 1];
			memcpy(received_stream_name, header->stream_name, VBAN_STREAM_NAME_MAX_LEN);
			received_stream_name[VBAN_STREAM_NAME_MAX_LEN] = '\0';

			if (strncmp(handle->ctx.receiver.config.expected_stream_name, received_stream_name, VBAN_STREAM_NAME_MAX_LEN) != 0) {
				ESP_LOGD(TAG, "Receive task: Stream name mismatch. Expected '%s', got '%s'", handle->ctx.receiver.config.expected_stream_name,
								 received_stream_name);
				continue;
			}
		}

		vban_sample_rate_index_t sr_idx;
		uint8_t sub_protocol_id;
		vban_util_parse_sr_subprotocol_byte(header->sr_subprotocol, &sr_idx, &sub_protocol_id);

		if (sub_protocol_id == (VBAN_SUBPROTOCOL_AUDIO >> VBAN_SUBPROTOCOL_SHIFT)) {	// Compare shifted value
			if (handle->ctx.receiver.config.audio_callback) {
				const uint8_t* audio_data = rx_buffer + VBAN_HEADER_SIZE;
				size_t audio_data_len = len - VBAN_HEADER_SIZE;

				// Optional: Validate audio_data_len based on header info
				vban_data_type_t data_type;
				uint8_t codec_id;
				bool reserved_bit;
				vban_util_parse_format_codec_byte(header->format_codec, &data_type, &codec_id, &reserved_bit);
				if (codec_id == (VBAN_CODEC_PCM >> VBAN_CODEC_SHIFT)) {	 // Compare shifted value
					size_t expected_payload_size =
							(size_t)(header->samples_per_frame_m1 + 1) * (header->channels_m1 + 1) * vban_get_data_type_size(data_type);
					if (audio_data_len != expected_payload_size) {
						ESP_LOGW(TAG, "Receive task: Audio data size mismatch. Expected %d, got %d. Frame %u, Stream '%s'", expected_payload_size,
										 audio_data_len, (unsigned)header->frame_counter, header->stream_name);
						// continue; // Or process anyway, depending on strictness
					}

					char sender_ip_str[16];
					inet_ntoa_r(source_addr.sin_addr, sender_ip_str, sizeof(sender_ip_str));

					handle->ctx.receiver.config.audio_callback(header, audio_data, audio_data_len, sender_ip_str, ntohs(source_addr.sin_port),
																										 handle->ctx.receiver.config.user_context);
				} else {
					ESP_LOGD(TAG, "Receive task: Received audio packet with unsupported codec ID %d", codec_id);
				}
			}
		} else {
			// ESP_LOGD(TAG, "Receive task: Received packet with sub-protocol %d (not audio)", sub_protocol_id);
			// Future: Handle other sub-protocols here
		}
	}

	ESP_LOGI(TAG, "VBAN Receiver task for stream '%s' stopping.",
					 handle->ctx.receiver.config.expected_stream_name[0] ? handle->ctx.receiver.config.expected_stream_name : "<ANY>");
	handle->ctx.receiver.receive_task_handle = NULL;	// Clear task handle as it's about to be deleted or has exited
	handle->ctx.receiver.state = VBAN_RECEIVER_STATE_IDLE;
	vTaskDelete(NULL);	// Task deletes itself
}

vban_handle_t vban_receiver_create(const vban_receiver_config_t* config) {
	if (!config || !config->audio_callback) {	 // audio_callback is mandatory for now
		ESP_LOGE(TAG, "Receiver create: Invalid arguments (callback missing)");
		return NULL;
	}
	if (strlen(config->expected_stream_name) >= VBAN_STREAM_NAME_MAX_LEN) {
		ESP_LOGE(TAG, "Receiver create: Expected stream name too long");
		return NULL;
	}

	vban_handle_t handle = (vban_handle_t)calloc(1, sizeof(struct vban_instance_s));
	if (!handle) {
		ESP_LOGE(TAG, "Receiver create: No memory for handle");
		return NULL;
	}

	handle->type = VBAN_INSTANCE_TYPE_RECEIVER;
	memcpy(&handle->ctx.receiver.config, config, sizeof(vban_receiver_config_t));
	handle->ctx.receiver.state = VBAN_RECEIVER_STATE_IDLE;
	handle->ctx.receiver.receive_task_handle = NULL;

	handle->sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (handle->sock_fd < 0) {
		ESP_LOGE(TAG, "Receiver create: Failed to create socket: %s", strerror(errno));
		free(handle);
		return NULL;
	}

	// Set socket to be non-blocking (optional, can help with cleaner task shutdown)
	// int flags = fcntl(handle->sock_fd, F_GETFL, 0);
	// fcntl(handle->sock_fd, F_SETFL, flags | O_NONBLOCK);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(config->listen_port > 0 ? config->listen_port : VBAN_DEFAULT_PORT);

	if (bind(handle->sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		ESP_LOGE(TAG, "Receiver create: Failed to bind socket to port %d: %s", ntohs(server_addr.sin_port), strerror(errno));
		close(handle->sock_fd);
		free(handle);
		return NULL;
	}

	ESP_LOGI(TAG, "VBAN Receiver created for stream '%s' on port %d",
					 config->expected_stream_name[0] ? config->expected_stream_name : "<ANY>", ntohs(server_addr.sin_port));
	return handle;
}

esp_err_t vban_receiver_delete(vban_handle_t handle) {
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_RECEIVER) {
		return ESP_ERR_VBAN_INVALID_HANDLE;
	}

	esp_err_t err = vban_receiver_stop(handle);	 // Ensure task is stopped
	if (err != ESP_OK && err != ESP_ERR_VBAN_NOT_STARTED) {
		ESP_LOGW(TAG, "Receiver delete: Failed to stop task cleanly, but proceeding with delete.");
	}
	// Wait a bit for the task to fully exit if it was running
	if (handle->ctx.receiver.receive_task_handle != NULL) {
		// This check might be racy if task deletes itself and sets handle to NULL.
		// A more robust way is to use a semaphore or event group for task exit confirmation.
		vTaskDelay(pdMS_TO_TICKS(50));
	}

	if (handle->sock_fd >= 0) {
		close(handle->sock_fd);
		handle->sock_fd = -1;
	}
	ESP_LOGI(TAG, "VBAN Receiver for stream '%s' deleted",
					 handle->ctx.receiver.config.expected_stream_name[0] ? handle->ctx.receiver.config.expected_stream_name : "<ANY>");
	free(handle);
	return ESP_OK;
}

esp_err_t vban_receiver_start(vban_handle_t handle) {
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_RECEIVER) {
		return ESP_ERR_VBAN_INVALID_HANDLE;
	}
	if (handle->ctx.receiver.state != VBAN_RECEIVER_STATE_IDLE || handle->ctx.receiver.receive_task_handle != NULL) {
		ESP_LOGW(TAG, "Receiver start: Already started or not idle.");
		return ESP_ERR_VBAN_ALREADY_STARTED;	// Or ESP_ERR_VBAN_INVALID_STATE
	}

	// Use configured or default task parameters
	const vban_receiver_config_t* cfg = &handle->ctx.receiver.config;
	BaseType_t xReturned = xTaskCreatePinnedToCore(vban_receive_task,
																								 "vban_rx_task",																												 // Task name
																								 cfg->task_stack_size > 0 ? cfg->task_stack_size : 4096,								 // Stack size
																								 (void*)handle,																													 // Parameter
																								 cfg->task_priority > 0 ? cfg->task_priority : (tskIDLE_PRIORITY + 5),	 // Priority
																								 &handle->ctx.receiver.receive_task_handle,															 // Task handle
																								 cfg->core_id == 0 || cfg->core_id == 1 ? cfg->core_id : tskNO_AFFINITY	 // Core ID
	);

	if (xReturned != pdPASS) {
		handle->ctx.receiver.receive_task_handle = NULL;
		ESP_LOGE(TAG, "Receiver start: Failed to create receiver task");
		return ESP_ERR_VBAN_TASK_CREATE_FAIL;
	}
	// State will be updated by the task itself once it starts running.
	// Add a small delay to allow task to start and set its state.
	vTaskDelay(pdMS_TO_TICKS(10));
	return ESP_OK;
}

esp_err_t vban_receiver_stop(vban_handle_t handle) {
	if (!handle || handle->type != VBAN_INSTANCE_TYPE_RECEIVER) {
		return ESP_ERR_VBAN_INVALID_HANDLE;
	}

	if (handle->ctx.receiver.state != VBAN_RECEIVER_STATE_RUNNING || !handle->ctx.receiver.receive_task_handle) {
		ESP_LOGI(TAG, "Receiver stop: Not running or no task handle.");
		// If task was created but never ran or already exited, set to idle.
		handle->ctx.receiver.state = VBAN_RECEIVER_STATE_IDLE;
		handle->ctx.receiver.receive_task_handle = NULL;
		return ESP_ERR_VBAN_NOT_STARTED;
	}

	handle->ctx.receiver.state = VBAN_RECEIVER_STATE_STOPPING;

	// The task checks this flag and should exit.
	// If the socket is blocking, recvfrom will keep blocking.
	// One way to unblock recvfrom is to close the socket from another task.
	// This can cause recvfrom to return an error, which the task should handle.
	// Closing socket here can be an option if task doesn't exit by flag alone.
	// For now, assume task loop handles the state change.
	// A more robust shutdown involves using an event or queue to signal the task,
	// or ensuring the socket has a timeout.

	// If socket is blocking and task is stuck in recvfrom, closing the socket
	// from this context might be necessary to unblock it.
	// This is a common pattern but can make error handling in the task tricky.
	// For simplicity, we'll rely on the task checking the state.
	// If using a non-blocking socket or a socket with timeout, this is cleaner.

	// Shutdown the socket to interrupt recvfrom if it's blocking
	if (handle->sock_fd >= 0) {
		shutdown(handle->sock_fd, SHUT_RDWR);	 // This should make recvfrom return.
	}

	// Wait for task to terminate (optional, with timeout)
	// For simplicity, we don't wait here. The task should clean itself up.
	// If vTaskDelete is called on receive_task_handle directly, it's not clean if task is running.
	// The task should delete itself (vTaskDelete(NULL)).

	ESP_LOGI(TAG, "Receiver stop: Signaled receiver task to stop.");
	// The task will set handle->ctx.receiver.receive_task_handle to NULL when it exits.
	// We cannot guarantee immediate stop here.
	return ESP_OK;
}