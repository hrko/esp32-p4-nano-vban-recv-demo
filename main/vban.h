#ifndef VBAN_H_
#define VBAN_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"  // For esp_err_t

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Constant Definitions
// -----------------------------------------------------------------------------

#define VBAN_DEFAULT_PORT 6980
#define VBAN_HEADER_SIZE 28
#define VBAN_MAX_PAYLOAD_SIZE 1436                                       // VBAN Data max size
#define VBAN_MAX_PACKET_SIZE (VBAN_HEADER_SIZE + VBAN_MAX_PAYLOAD_SIZE)  // 1464 bytes
#define VBAN_STREAM_NAME_MAX_LEN 16
#define VBAN_MAGIC_NUMBER 0x4E414256  // 'VBAN' (In little-endian, 'N','A','B','V')

// Sub-protocol Identifiers - Page 8
#define VBAN_SUBPROTOCOL_AUDIO 0x00
#define VBAN_SUBPROTOCOL_SERIAL 0x20
#define VBAN_SUBPROTOCOL_TEXT 0x40
#define VBAN_SUBPROTOCOL_SERVICE 0x60
// Other sub-protocols will be added in the future

// Audio Codec Identifiers - Page 10
#define VBAN_CODEC_PCM 0x00
// Other codecs will be added in the future

// Masks and shifts for SR_SUBPROTOCOL byte
#define VBAN_SR_INDEX_MASK 0x1F     // Bits 0-4 for Sample Rate Index
#define VBAN_SUBPROTOCOL_MASK 0xE0  // Bits 5-7 for Sub-protocol
#define VBAN_SUBPROTOCOL_SHIFT 5

// Masks and shifts for FORMAT_CODEC byte
#define VBAN_DATATYPE_MASK 0x07      // Bits 0-2 for Data Type
#define VBAN_RESERVED_BIT_MASK 0x08  // Bit 3 (Reserved, must be 0 for PCM)
#define VBAN_CODEC_MASK 0xF0         // Bits 4-7 for Codec
#define VBAN_CODEC_SHIFT 4

// -----------------------------------------------------------------------------
// Error Code Definitions
// -----------------------------------------------------------------------------
#define ESP_ERR_VBAN_BASE 0x70000  // Base for VBAN errors
#define ESP_ERR_VBAN_INVALID_ARG (ESP_ERR_VBAN_BASE + 1)
#define ESP_ERR_VBAN_NO_MEM (ESP_ERR_VBAN_BASE + 2)
#define ESP_ERR_VBAN_SOCKET_ERR (ESP_ERR_VBAN_BASE + 3)
#define ESP_ERR_VBAN_INVALID_HANDLE (ESP_ERR_VBAN_BASE + 4)
#define ESP_ERR_VBAN_SEND_FAIL (ESP_ERR_VBAN_BASE + 5)
#define ESP_ERR_VBAN_RECEIVE_FAIL (ESP_ERR_VBAN_BASE + 6)
#define ESP_ERR_VBAN_INVALID_PACKET (ESP_ERR_VBAN_BASE + 7)
#define ESP_ERR_VBAN_STREAM_NAME_MISMATCH (ESP_ERR_VBAN_BASE + 8)
#define ESP_ERR_VBAN_WRONG_SUBPROTOCOL (ESP_ERR_VBAN_BASE + 9)
#define ESP_ERR_VBAN_TASK_CREATE_FAIL (ESP_ERR_VBAN_BASE + 10)
#define ESP_ERR_VBAN_ALREADY_STARTED (ESP_ERR_VBAN_BASE + 11)
#define ESP_ERR_VBAN_NOT_STARTED (ESP_ERR_VBAN_BASE + 12)
#define ESP_ERR_VBAN_INVALID_STATE (ESP_ERR_VBAN_BASE + 13)
#define ESP_ERR_VBAN_PAYLOAD_TOO_LARGE (ESP_ERR_VBAN_BASE + 14)
#define ESP_ERR_VBAN_DATA_SIZE_MISMATCH (ESP_ERR_VBAN_BASE + 15)

// -----------------------------------------------------------------------------
// Data Structure Definitions
// -----------------------------------------------------------------------------

/**
 * @brief VBAN Packet Header Structure (28 bytes) - Page 6
 * Follows little-endian rules.
 */
typedef struct {
  uint32_t vban_magic;                         ///< 'VBAN' (Stored as 0x4E414256 for 'N','A','B','V' in memory due to little-endian)
  uint8_t sr_subprotocol;                      ///< 5 LSB for SR index (0-31), 3 MSB for Sub-Protocol (0-7)
  uint8_t samples_per_frame_m1;                ///< Number of samples per frame minus 1 (0=1 sample, 255=256 samples)
  uint8_t channels_m1;                         ///< Number of channels minus 1 (0=1 channel, 255=256 channels)
  uint8_t format_codec;                        ///< 3 LSB for Data Format (0-7), 1 bit reserved (must be 0), 4 MSB for Codec (0-15)
  char stream_name[VBAN_STREAM_NAME_MAX_LEN];  ///< Stream Name (ASCII, null-terminated if shorter than 16)
  uint32_t frame_counter;                      ///< Growing frame number (for error detection)
} __attribute__((packed)) vban_header_t;

/**
 * @brief Sample Rate Index for VBAN - Page 8
 */
typedef enum {
  VBAN_SR_6000 = 0,
  VBAN_SR_12000 = 1,
  VBAN_SR_24000 = 2,
  VBAN_SR_48000 = 3,
  VBAN_SR_96000 = 4,
  VBAN_SR_192000 = 5,
  VBAN_SR_384000 = 6,
  VBAN_SR_8000 = 7,
  VBAN_SR_16000 = 8,
  VBAN_SR_32000 = 9,
  VBAN_SR_64000 = 10,
  VBAN_SR_128000 = 11,
  VBAN_SR_256000 = 12,
  VBAN_SR_512000 = 13,
  VBAN_SR_11025 = 14,
  VBAN_SR_22050 = 15,
  VBAN_SR_44100 = 16,
  VBAN_SR_88200 = 17,
  VBAN_SR_176400 = 18,
  VBAN_SR_352800 = 19,
  VBAN_SR_705600 = 20,
  VBAN_SR_UNDEFINED_21,
  VBAN_SR_UNDEFINED_22,
  VBAN_SR_UNDEFINED_23,
  VBAN_SR_UNDEFINED_24,
  VBAN_SR_UNDEFINED_25,
  VBAN_SR_UNDEFINED_26,
  VBAN_SR_UNDEFINED_27,
  VBAN_SR_UNDEFINED_28,
  VBAN_SR_UNDEFINED_29,
  VBAN_SR_UNDEFINED_30,
  VBAN_SR_UNDEFINED_31,
  VBAN_SR_MAX_INDEX  // Should be 21 for defined SRs
} vban_sample_rate_index_t;

/**
 * @brief Audio Data Type (Bit Resolution) for VBAN - Page 9
 */
typedef enum {
  VBAN_DATATYPE_UINT8 = 0,    ///< Unsigned 8-bit PCM (0-255, 128=0)
  VBAN_DATATYPE_INT16 = 1,    ///< Signed 16-bit PCM (-32768 to 32767)
  VBAN_DATATYPE_INT24 = 2,    ///< Signed 24-bit PCM (stored in 3 bytes)
  VBAN_DATATYPE_INT32 = 3,    ///< Signed 32-bit PCM
  VBAN_DATATYPE_FLOAT32 = 4,  ///< 32-bit float PCM (-1.0 to +1.0)
  VBAN_DATATYPE_FLOAT64 = 5,  ///< 64-bit float PCM (-1.0 to +1.0)
  VBAN_DATATYPE_INT12 = 6,    ///< Signed 12-bit PCM (uncommon, packed)
  VBAN_DATATYPE_INT10 = 7     ///< Signed 10-bit PCM (uncommon, packed)
} vban_data_type_t;

/**
 * @brief VBAN Audio Format Configuration
 */
typedef struct {
  vban_sample_rate_index_t sample_rate_idx;  ///< Sample rate index
  uint8_t num_channels;                      ///< Number of channels (1-256)
  vban_data_type_t data_type;                ///< Audio data type (bit resolution)
  // vban_codec_t             codec;        // For now, only PCM is supported, implicitly VBAN_CODEC_PCM
} vban_audio_format_t;

/**
 * @brief VBAN Sender Configuration
 */
typedef struct {
  char stream_name[VBAN_STREAM_NAME_MAX_LEN];  ///< Name of the VBAN stream to send
  char dest_ip[16];                            ///< Destination IP address (e.g., "192.168.1.100")
  uint16_t dest_port;                          ///< Destination UDP port (default: VBAN_DEFAULT_PORT)
  vban_audio_format_t audio_format;            ///< Format of the audio to be sent
  // uint8_t sub_protocol;                    // For future expansion, default VBAN_SUBPROTOCOL_AUDIO
} vban_sender_config_t;

/**
 * @brief Callback function type for received VBAN audio data.
 *
 * @param header Pointer to the received VBAN header.
 * @param audio_data Pointer to the start of the audio payload.
 * @param audio_data_len Length of the audio payload in bytes.
 * @param sender_ip IP address of the sender.
 * @param sender_port Port of the sender.
 * @param user_context User context provided during receiver creation.
 */
typedef void (*vban_audio_receive_callback_t)(const vban_header_t* header, const uint8_t* audio_data, size_t audio_data_len,
                                              const char* sender_ip, uint16_t sender_port, void* user_context);

/**
 * @brief VBAN Receiver Configuration
 */
typedef struct {
  char expected_stream_name[VBAN_STREAM_NAME_MAX_LEN];  ///< Only process packets with this stream name (empty to accept any)
  uint16_t listen_port;                                 ///< UDP port to listen on (default: VBAN_DEFAULT_PORT)
  vban_audio_receive_callback_t audio_callback;         ///< Callback for received audio packets
  void* user_context;                                   ///< User context for the callback
  // uint8_t accepted_sub_protocols_mask;              // For future expansion
  int core_id;             ///< CPU core to run the receiver task on (0, 1, or tskNO_AFFINITY)
  int task_priority;       ///< Priority of the receiver task (1-configMAX_PRIORITIES-1)
  size_t task_stack_size;  ///< Stack size for the receiver task (e.g., 4096)
} vban_receiver_config_t;

/**
 * @brief Opaque handle for a VBAN instance (sender or receiver).
 */
typedef struct vban_instance_s* vban_handle_t;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

/**
 * @brief Create a VBAN sender instance.
 *
 * @param config Configuration for the sender.
 * @return Handle to the VBAN sender instance, or NULL on failure.
 * Use esp_err_to_name() to get error string if needed, though direct error code is not returned here.
 * Check for NULL handle.
 */
vban_handle_t vban_sender_create(const vban_sender_config_t* config);

/**
 * @brief Delete a VBAN sender instance and release resources.
 *
 * @param handle Handle to the VBAN sender instance.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t vban_sender_delete(vban_handle_t handle);

/**
 * @brief Send an VBAN audio packet.
 *
 * The audio_data must be interleaved PCM samples.
 * The total size of the audio data is (num_samples * num_channels * bytes_per_sample_component).
 * This function will construct the VBAN header and send the UDP packet.
 *
 * @param handle Handle to the VBAN sender instance.
 * @param audio_data Pointer to the audio data to send.
 * @param num_samples Number of samples per channel in this packet (1-256).
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t vban_audio_send(vban_handle_t handle, const void* audio_data, uint8_t num_samples);

/**
 * @brief Create a VBAN receiver instance.
 * This does not start the receiver task yet. Call vban_receiver_start() to begin listening.
 *
 * @param config Configuration for the receiver.
 * @return Handle to the VBAN receiver instance, or NULL on failure.
 */
vban_handle_t vban_receiver_create(const vban_receiver_config_t* config);

/**
 * @brief Delete a VBAN receiver instance and release resources.
 * If the receiver task is running, it will be stopped first.
 *
 * @param handle Handle to the VBAN receiver instance.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t vban_receiver_delete(vban_handle_t handle);

/**
 * @brief Start the VBAN receiver task.
 * The receiver will start listening for UDP packets and invoking the callback.
 *
 * @param handle Handle to the VBAN receiver instance.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t vban_receiver_start(vban_handle_t handle);

/**
 * @brief Stop the VBAN receiver task.
 *
 * @param handle Handle to the VBAN receiver instance.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t vban_receiver_stop(vban_handle_t handle);

// --- Utility Functions (can be made static in .c if not needed externally) ---

/**
 * @brief Get the size of a single sample component in bytes based on VBAN data type.
 *
 * @param data_type The VBAN data type.
 * @return Size in bytes (e.g., 2 for INT16, 3 for INT24). Returns 0 for invalid type.
 */
size_t vban_get_data_type_size(vban_data_type_t data_type);

/**
 * @brief Get the actual sample rate value from VBAN sample rate index.
 *
 * @param sr_idx The VBAN sample rate index.
 * @return Actual sample rate (e.g., 48000), or 0 if index is invalid.
 */
uint32_t vban_get_sr_from_index(vban_sample_rate_index_t sr_idx);

/**
 * @brief Get the VBAN sample rate index from an actual sample rate value.
 *
 * @param sample_rate Actual sample rate (e.g., 44100).
 * @return The VBAN sample rate index, or VBAN_SR_MAX_INDEX if not found.
 */
vban_sample_rate_index_t vban_get_index_from_sr(uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif  // VBAN_H_