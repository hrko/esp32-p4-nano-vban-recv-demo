#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdbool.h>	// For bool
#include <stddef.h>		// For size_t

// --- エラーコード定義 ---
#define CB_SUCCESS 0									// 成功
#define CB_ERROR_INVALID_ARG -1				// 無効な引数
#define CB_ERROR_NOT_INITIALIZED -2		// 未初期化
#define CB_ERROR_BUFFER_FULL -3				// バッファ満杯
#define CB_ERROR_ALLOC_FAILED -4			// メモリ確保失敗
#define CB_ERROR_CONSUME_TOO_MUCH -5	// 消費量が格納データ量を超過

/**
 * @brief 循環バッファ構造体
 */
typedef struct {
	char *buffer;				 /**< 内部データバッファ (実際のサイズは capacity * 2) */
	size_t capacity;		 /**< 論理的なバッファ容量 (ユーザー指定サイズ) */
	size_t head;				 /**< 書き込み位置の論理インデックス (0 から capacity-1) */
	size_t tail;				 /**< 読み出し位置の論理インデックス (0 から capacity-1) */
	size_t count;				 /**< 現在バッファに格納されているデータのバイト数 */
	bool is_initialized; /**< 初期化済みフラグ */
} circular_buffer_t;

/**
 * @brief 循環バッファを初期化する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @param capacity バッファの論理容量（バイト単位）
 * @return 成功時は CB_SUCCESS、エラー時は負のエラーコード
 */
int circular_buffer_init(circular_buffer_t *cb, size_t capacity);

/**
 * @brief 循環バッファを破棄し、確保したメモリを解放する
 *
 * @param cb 循環バッファ構造体へのポインタ
 */
void circular_buffer_destroy(circular_buffer_t *cb);

/**
 * @brief 循環バッファにデータを書き込む
 * データは内部バッファにコピーされる。
 * 内部ではミラーリングのため2箇所にコピーされる。
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @param data 書き込むデータへのポインタ
 * @param bytes 書き込むデータのバイト数
 * @return 成功時は CB_SUCCESS、エラー時は負のエラーコード
 */
int circular_buffer_write(circular_buffer_t *cb, const void *data, size_t bytes);

/**
 * @brief 読み出し可能な連続したデータ領域へのポインタを取得する
 * この関数はバッファの状態を変更しない（読み出し位置は進めない）。
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @param[out] readable_bytes 読み出し可能な連続データのバイト数が格納されるポインタ
 * @return 読み出し可能なデータ領域へのポインタ。エラー時やデータがない場合は NULL。
 */
void *circular_buffer_get_readable_region(circular_buffer_t *cb, size_t *readable_bytes);

/**
 * @brief バッファからデータを消費（読み出し完了を通知）する
 * 指定されたバイト数だけ読み出し位置を進める。
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @param bytes_consumed 消費したデータのバイト数
 * @return 成功時は CB_SUCCESS、エラー時は負のエラーコード
 */
int circular_buffer_consume(circular_buffer_t *cb, size_t bytes_consumed);

/**
 * @brief バッファに格納されているデータのバイト数を取得する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @return 格納されているデータのバイト数。cbがNULLまたは未初期化の場合は0。
 */
size_t circular_buffer_get_count(const circular_buffer_t *cb);

/**
 * @brief バッファの論理容量を取得する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @return バッファの論理容量。cbがNULLまたは未初期化の場合は0。
 */
size_t circular_buffer_get_capacity(const circular_buffer_t *cb);

/**
 * @brief バッファの空き容量を取得する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @return バッファの空き容量。cbがNULLまたは未初期化の場合は0。
 */
size_t circular_buffer_get_free_space(const circular_buffer_t *cb);

/**
 * @brief バッファが空かどうかを確認する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @return バッファが空の場合は true、そうでない場合は false。
 * cbがNULLまたは未初期化の場合は true (空とみなす)。
 */
bool circular_buffer_is_empty(const circular_buffer_t *cb);

/**
 * @brief バッファが満杯かどうかを確認する
 *
 * @param cb 循環バッファ構造体へのポインタ
 * @return バッファが満杯の場合は true、そうでない場合は false。
 * cbがNULLまたは未初期化の場合は false (満杯ではないとみなす)。
 */
bool circular_buffer_is_full(const circular_buffer_t *cb);

#endif	// CIRCULAR_BUFFER_H
