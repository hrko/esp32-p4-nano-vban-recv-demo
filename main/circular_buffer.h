#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdbool.h>	// For bool
#include <stddef.h>		// For size_t

// --- Error code definitions ---
#define CB_SUCCESS 0									// Success
#define CB_ERROR_INVALID_ARG -1				// Invalid argument
#define CB_ERROR_NOT_INITIALIZED -2		// Not initialized
#define CB_ERROR_BUFFER_FULL -3				// Buffer full
#define CB_ERROR_ALLOC_FAILED -4			// Memory allocation failed
#define CB_ERROR_CONSUME_TOO_MUCH -5	// Consumed amount exceeds stored data

/**
 * @brief Circular buffer structure
 */
typedef struct {
	char *buffer;				 /**< Internal data buffer (actual size is capacity * 2) */
	size_t capacity;		 /**< Logical buffer capacity (user-specified size) */
	size_t head;				 /**< Logical index of write position (0 to capacity-1) */
	size_t tail;				 /**< Logical index of read position (0 to capacity-1) */
	size_t count;				 /**< Number of bytes currently stored in the buffer */
	bool is_initialized; /**< Initialization flag */
} circular_buffer_t;

/**
 * @brief Initialize the circular buffer
 *
 * @param cb Pointer to the circular buffer structure
 * @param capacity Buffer logical capacity (in bytes)
 * @return CB_SUCCESS on success, negative error code on failure
 */
int circular_buffer_init(circular_buffer_t *cb, size_t capacity);

/**
 * @brief Destroy the circular buffer and free allocated memory
 *
 * @param cb Pointer to the circular buffer structure
 */
void circular_buffer_destroy(circular_buffer_t *cb);

/**
 * @brief Write data to the circular buffer
 * The data is copied into the internal buffer.
 * Internally, the data is copied to two locations for mirroring.
 *
 * @param cb Pointer to the circular buffer structure
 * @param data Pointer to the data to write
 * @param bytes Number of bytes to write
 * @return CB_SUCCESS on success, negative error code on failure
 */
int circular_buffer_write(circular_buffer_t *cb, const void *data, size_t bytes);

/**
 * @brief Get a pointer to a contiguous readable data region
 * This function does not modify the buffer state (does not advance the read position).
 *
 * @param cb Pointer to the circular buffer structure
 * @param[out] readable_bytes Pointer to store the number of readable contiguous bytes
 * @return Pointer to the readable data region. Returns NULL on error or if no data is available.
 */
void *circular_buffer_get_readable_region(circular_buffer_t *cb, size_t *readable_bytes);

/**
 * @brief Consume (notify completion of reading) data from the buffer
 * Advances the read position by the specified number of bytes.
 *
 * @param cb Pointer to the circular buffer structure
 * @param bytes_consumed Number of bytes of data consumed
 * @return CB_SUCCESS on success, negative error code on failure
 */
int circular_buffer_consume(circular_buffer_t *cb, size_t bytes_consumed);

/**
 * @brief Get the number of bytes stored in the buffer
 *
 * @param cb Pointer to the circular buffer structure
 * @return Number of bytes stored. Returns 0 if cb is NULL or uninitialized.
 */
size_t circular_buffer_get_count(const circular_buffer_t *cb);

/**
 * @brief Get the logical capacity of the buffer
 *
 * @param cb Pointer to the circular buffer structure
 * @return Logical capacity of the buffer. Returns 0 if cb is NULL or uninitialized.
 */
size_t circular_buffer_get_capacity(const circular_buffer_t *cb);

/**
 * @brief Get the free space in the buffer
 *
 * @param cb Pointer to the circular buffer structure
 * @return Free space in the buffer. Returns 0 if cb is NULL or uninitialized.
 */
size_t circular_buffer_get_free_space(const circular_buffer_t *cb);

/**
 * @brief Check if the buffer is empty
 *
 * @param cb Pointer to the circular buffer structure
 * @return true if the buffer is empty, false otherwise.
 * Returns true (considered empty) if cb is NULL or uninitialized.
 */
bool circular_buffer_is_empty(const circular_buffer_t *cb);

/**
 * @brief Check if the buffer is full
 *
 * @param cb Pointer to the circular buffer structure
 * @return true if the buffer is full, false otherwise.
 * Returns false (not considered full) if cb is NULL or uninitialized.
 */
bool circular_buffer_is_full(const circular_buffer_t *cb);

#endif	// CIRCULAR_BUFFER_H
