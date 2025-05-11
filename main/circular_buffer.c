#include "circular_buffer.h"

#include <stdlib.h>	 // For malloc, free
#include <string.h>	 // For memcpy

// --- Helper Macros for precondition checking ---
#define CB_ENSURE_VALID_CB_PTR(cb_ptr) \
	if (!(cb_ptr)) return CB_ERROR_INVALID_ARG;

#define CB_ENSURE_INITIALIZED(cb_ptr) \
	CB_ENSURE_VALID_CB_PTR(cb_ptr)      \
	if (!(cb_ptr)->is_initialized) return CB_ERROR_NOT_INITIALIZED;

int circular_buffer_init(circular_buffer_t *cb, size_t capacity) {
	if (!cb || capacity == 0) {
		return CB_ERROR_INVALID_ARG;
	}

	// The internal buffer is allocated at twice the specified capacity
	cb->buffer = (char *)malloc(capacity * 2);
	if (!cb->buffer) {
		cb->is_initialized = false;	 // Set to uninitialized state just in case
		return CB_ERROR_ALLOC_FAILED;
	}

	cb->capacity = capacity;
	cb->head = 0;
	cb->tail = 0;
	cb->count = 0;
	cb->is_initialized = true;

	return CB_SUCCESS;
}

void circular_buffer_destroy(circular_buffer_t *cb) {
	if (cb && cb->is_initialized) {
		free(cb->buffer);
		cb->buffer = NULL;	// Set to NULL after freeing
		cb->capacity = 0;
		cb->head = 0;
		cb->tail = 0;
		cb->count = 0;
		cb->is_initialized = false;
	}
}

int circular_buffer_write(circular_buffer_t *cb, const void *data, size_t bytes) {
	CB_ENSURE_INITIALIZED(cb);

	if (!data && bytes > 0) {	 // data is NULL even though bytes > 0 is invalid
		return CB_ERROR_INVALID_ARG;
	}
	if (bytes == 0) {	 // If there is no data to write, succeed
		return CB_SUCCESS;
	}

	if (circular_buffer_get_free_space(cb) < bytes) {
		return CB_ERROR_BUFFER_FULL;
	}

	const char *data_char = (const char *)data;
	size_t current_logical_head = cb->head;

	// Copy data into two regions
	// 1. From head to the end of the logical buffer
	size_t len_part1 = cb->capacity - current_logical_head;
	if (len_part1 > bytes) {
		len_part1 = bytes;
	}
	memcpy(cb->buffer + current_logical_head, data_char, len_part1);
	memcpy(cb->buffer + current_logical_head + cb->capacity, data_char, len_part1);	 // Mirror region

	// 2. From the beginning of the logical buffer for the remainder (if wrap-around occurs)
	size_t remaining_bytes = bytes - len_part1;
	if (remaining_bytes > 0) {
		memcpy(cb->buffer, data_char + len_part1, remaining_bytes);
		memcpy(cb->buffer + cb->capacity, data_char + len_part1, remaining_bytes);	// Mirror region
	}

	cb->head = (current_logical_head + bytes) % cb->capacity;
	cb->count += bytes;

	return CB_SUCCESS;
}

void *circular_buffer_get_readable_region(circular_buffer_t *cb, size_t *readable_bytes) {
	if (!readable_bytes) {	// Output pointer is NULL
		return NULL;
	}
	if (!cb) {	// Buffer structure itself is NULL
		*readable_bytes = 0;
		return NULL;
	}
	if (!cb->is_initialized) {	// Not initialized
		*readable_bytes = 0;
		return NULL;
	}

	if (cb->count == 0) {	 // Buffer is empty
		*readable_bytes = 0;
		return NULL;
	}

	*readable_bytes = cb->count;
	// Return a contiguous region starting from tail (thanks to mirroring, count bytes are contiguous)
	return (void *)(cb->buffer + cb->tail);
}

int circular_buffer_consume(circular_buffer_t *cb, size_t bytes_consumed) {
	CB_ENSURE_INITIALIZED(cb);

	if (bytes_consumed == 0) {	// If there is no data to consume, succeed
		return CB_SUCCESS;
	}

	if (bytes_consumed > cb->count) {	 // Trying to consume more than stored data
		return CB_ERROR_CONSUME_TOO_MUCH;
	}

	cb->tail = (cb->tail + bytes_consumed) % cb->capacity;
	cb->count -= bytes_consumed;

	return CB_SUCCESS;
}

size_t circular_buffer_get_count(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return 0;
	}
	return cb->count;
}

size_t circular_buffer_get_capacity(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return 0;
	}
	return cb->capacity;
}

size_t circular_buffer_get_free_space(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return 0;
	}
	return cb->capacity - cb->count;
}

bool circular_buffer_is_empty(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return true;	// Consider uninitialized etc. as empty
	}
	return cb->count == 0;
}

bool circular_buffer_is_full(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return false;	 // Consider uninitialized etc. as not full
	}
	return cb->count == cb->capacity;
}
