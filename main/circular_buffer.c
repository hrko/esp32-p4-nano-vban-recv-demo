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

	// 内部バッファは指定容量の2倍確保
	cb->buffer = (char *)malloc(capacity * 2);
	if (!cb->buffer) {
		cb->is_initialized = false;	 // 念のため初期化失敗状態に
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
		cb->buffer = NULL;	// 解放後はNULLにしておく
		cb->capacity = 0;
		cb->head = 0;
		cb->tail = 0;
		cb->count = 0;
		cb->is_initialized = false;
	}
}

int circular_buffer_write(circular_buffer_t *cb, const void *data, size_t bytes) {
	CB_ENSURE_INITIALIZED(cb);

	if (!data && bytes > 0) {	 // bytes > 0 なのに data が NULL は無効
		return CB_ERROR_INVALID_ARG;
	}
	if (bytes == 0) {	 // 書き込むデータがない場合は成功
		return CB_SUCCESS;
	}

	if (circular_buffer_get_free_space(cb) < bytes) {
		return CB_ERROR_BUFFER_FULL;
	}

	const char *data_char = (const char *)data;
	size_t current_logical_head = cb->head;

	// データを2つの領域にコピーする
	// 1. headから論理バッファの終わりまで
	size_t len_part1 = cb->capacity - current_logical_head;
	if (len_part1 > bytes) {
		len_part1 = bytes;
	}
	memcpy(cb->buffer + current_logical_head, data_char, len_part1);
	memcpy(cb->buffer + current_logical_head + cb->capacity, data_char, len_part1);	 // ミラー領域

	// 2. 論理バッファの先頭から残り (ラップアラウンドする場合)
	size_t remaining_bytes = bytes - len_part1;
	if (remaining_bytes > 0) {
		memcpy(cb->buffer, data_char + len_part1, remaining_bytes);
		memcpy(cb->buffer + cb->capacity, data_char + len_part1, remaining_bytes);	// ミラー領域
	}

	cb->head = (current_logical_head + bytes) % cb->capacity;
	cb->count += bytes;

	return CB_SUCCESS;
}

void *circular_buffer_get_readable_region(circular_buffer_t *cb, size_t *readable_bytes) {
	if (!readable_bytes) {	// 出力先ポインタがNULL
		return NULL;
	}
	if (!cb) {	// バッファ構造体自体がNULL
		*readable_bytes = 0;
		return NULL;
	}
	if (!cb->is_initialized) {	// 未初期化
		*readable_bytes = 0;
		return NULL;
	}

	if (cb->count == 0) {	 // バッファが空
		*readable_bytes = 0;
		return NULL;
	}

	*readable_bytes = cb->count;
	// tail から始まる連続した領域を返す (ミラーリングのおかげで count バイト連続している)
	return (void *)(cb->buffer + cb->tail);
}

int circular_buffer_consume(circular_buffer_t *cb, size_t bytes_consumed) {
	CB_ENSURE_INITIALIZED(cb);

	if (bytes_consumed == 0) {	// 消費するデータがない場合は成功
		return CB_SUCCESS;
	}

	if (bytes_consumed > cb->count) {	 // 格納データ量より多く消費しようとした
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
		return true;	// 未初期化などは空とみなす
	}
	return cb->count == 0;
}

bool circular_buffer_is_full(const circular_buffer_t *cb) {
	if (!cb || !cb->is_initialized) {
		return false;	 // 未初期化などは満杯ではないとみなす
	}
	return cb->count == cb->capacity;
}
