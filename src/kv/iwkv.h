#pragma once
#ifndef IWKV_H
#define IWKV_H

/**************************************************************************************************
 * IOWOW library
 *
 * MIT License
 *
 * Copyright (c) 2012-2018 Softmotions Ltd <info@softmotions.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *************************************************************************************************/

/** @file
 *  @brief Persistent key-value storage based on skiplist
 *         datastructure (https://en.wikipedia.org/wiki/Skip_list).
 *  @author Anton Adamansky (adamansky@softmotions.com)
 *
 * <strong>Features:<strong>
 * - Simple design of key value storage
 * - Lightweight shared/static library: 200Kb
 * - Support of multiple key-value databases within a single file
 * - Ultra-fast traversal of database records
 * - Native support of integer keys
 * - Support of record values represented as sorted array of integers
 *
 * <strong>Limitations:<strong>
 * - Maximum iwkv storage file size: 255 GB (0x3fffffffc0)
 * - Total size of a single key+value record must be not greater than 255Mb (0xfffffff)
 * - In-memory cache for every opened database takes ~130Kb, cache can be disposed by `iwkv_db_cache_release()`
 */

#include "iowow.h"
#include "iwfile.h"
#include <stddef.h>
#include <stdbool.h>

IW_EXTERN_C_START

/**
 * @brief IWKV error codes.
 */
typedef enum {
  _IWKV_ERROR_START = (IW_ERROR_START + 5000UL),
  IWKV_ERROR_NOTFOUND,             /**< Key not found (IWKV_ERROR_NOTFOUND) */
  IWKV_ERROR_KEY_EXISTS,           /**< Key already exists (IWKV_ERROR_KEY_EXISTS) */
  IWKV_ERROR_MAXKVSZ,              /**< Size of Key+value must be not greater than 0xfffffff bytes (IWKV_ERROR_MAXKVSZ) */
  IWKV_ERROR_CORRUPTED,            /**< Database file invalid or corrupted (IWKV_ERROR_CORRUPTED) */
  IWKV_ERROR_DUP_VALUE_SIZE,       /**< Value size is not compatible for insertion into sorted values array (IWKV_ERROR_DUP_VALUE_SIZE) */
  IWKV_ERROR_KEY_NUM_VALUE_SIZE,   /**< Given key is not compatible to storage as number (IWKV_ERROR_KEY_NUM_VALUE_SIZE)  */
  IWKV_ERROR_INCOMPATIBLE_DB_MODE, /**< Incorpatible database open mode (IWKV_ERROR_INCOMPATIBLE_DB_MODE) */
  _IWKV_ERROR_END,
  /* Internal error codes */
  _IWKV_ERROR_KVBLOCK_FULL,
  _IWKV_ERROR_REQUIRE_NLEVEL,
} iwkv_ecode;

/**
 * @brief Database file open modes.
 */
typedef enum {
  IWKV_NOLOCKS  = 0x1,      /**< Do not use any locking on storage file (assumed single threaded app) */
  IWKV_RDONLY   = 0x2,      /**< Open storage file in read-only mode */
  IWKV_TRUNC    = 0x4       /**< Truncate storage file on open */
} iwkv_openflags;

/**
 * @brief Database initialization modes.
 */
typedef enum {
  IWDB_UINT32_KEYS = 0x1,     /**< Database keys are 32bit unsigned integers */
  IWDB_UINT64_KEYS = 0x2,     /**< Database keys are 64bit unsigned integers */
  IWDB_DUP_UINT32_VALS = 0x4, /**< Record key value is an array of sorted uint32 values */
  IWDB_DUP_UINT64_VALS = 0x8  /**< Record key value is an array of sorted uint64 values */
} iwdb_flags_t;

/**
 * @brief Record store modes used in `iwkv_put()` and `iwkv_cursor_set()` functions.
 */
typedef enum {
  IWKV_NO_OVERWRITE = 0x1,   /**< Do not overwrite value for an existing key.
                                  IWKV_ERROR_KEY_EXISTS will be fired in such cases. */
  IWKV_DUP_REMOVE =   0x2,   /**< Remove value from duplicated values array.
                                  Usable only for IWDB_DUP_XXX DB database modes */
  IWKV_SYNC =         0x4    /**< Flush changes on disk after operation */
} iwkv_opflags;

struct IWKV;
typedef struct IWKV *IWKV;

struct IWDB;
typedef struct IWDB *IWDB;

/**
 * @brief IWKV storage open options.
 */
typedef struct IWKV_OPTS {
  char *path;              /**< Path to database file */
  int32_t random_seed;     /**< Random seed used for iwu random generator */
  iwkv_openflags oflags;   /**< Bitmask of database file open modes */
} IWKV_OPTS;

/**
 * @brief Data container for key/value.
 */
typedef struct IWKV_val {
  void  *data;            /**< Data buffer */
  size_t  size;           /**< Data buffer size */
} IWKV_val;

/**
 * @brief Cursor opaque handler.
 */
struct IWKV_cursor;
typedef struct IWKV_cursor *IWKV_cursor;

/**
 * @brief Database cursor operations and position flags.
 */
typedef enum IWKV_cursor_op {
  IWKV_CURSOR_BEFORE_FIRST = 1, /**< Set cursor to position before first record */
  IWKV_CURSOR_AFTER_LAST,       /**< Set cursor to position after last record */
  IWKV_CURSOR_NEXT,             /**< Move cursor to the next record */
  IWKV_CURSOR_PREV,             /**< Move cursor to the previous record */
  IWKV_CURSOR_EQ,               /**< Set cursor to the specified key value */
  IWKV_CURSOR_GE                /**< Set cursor to the key which greater of equal key specified */
} IWKV_cursor_op;

/**
 * @brief Initialize iwkv storage.
 * @details This method must be called before using of any iwkv public API function.
 * @note iwkv implicitly initialized by iw_init()
 */
IW_EXPORT WUR iwrc iwkv_init(void);

/**
 * @brief Open iwkv storage.
 * @code {.c}
 *  IWKV iwkv;
 *  IWKV_OPTS opts = {
 *    .path = "mystore.db"
 *  };
 *  iwrc rc = iwkv_open(&opts, &iwkv);
 * @endcode
 * @note Any opened iwkv storage must be closed by `iwkv_close()` after usage.
 * @param opts Database open options.
 * @param [out] iwkvp Pointer to @ref IWKV structure.
 */
IW_EXPORT WUR iwrc iwkv_open(const IWKV_OPTS *opts, IWKV *iwkvp);

/**
 * @brief Get iwkv database handler identified by specified `dbid` number.
 * @details In the case if no database matched `dbid`
 *          a new database will be created using specified function arguments.
 *
 * @note Database handler doesn't require to be explicitly closed or freed.
 *       Although it may be usefull to cleanup database cache memory of unused databases
 *       dependening on memory requirements of your application.
 * @note Database `flags` argument must be same for all subsequent
 *       calls after first call for particular database,
 *       otherwise `IWKV_ERROR_INCOMPATIBLE_DB_MODE` will be reported.
 *
 * @param iwkv Pointer to @ref IWKV storage handler
 * @param dbid Database identifier
 * @param flags Database initialization flags
 * @param [out] dbp Pointer to database opaque structure.
 */
IW_EXPORT WUR iwrc iwkv_db(IWKV iwkv, uint32_t dbid, iwdb_flags_t flags, IWDB *dbp);

/**
 * @brief Frees memory resources used by database cache
 *        until to next database access operation (get/put/cursor).
 *        It will free about ~130Kb of memory per database in use.
 *
 * @param db Database handler
 */
IW_EXPORT iwrc iwkv_db_cache_release(IWDB db);

/**
 * @brief Get last access time (ms since epoch) of database get/put/cursor operation.
 * @details Returns `0` if database was not used before: no get/put/cursor operations used.
 *
 * @param db Database handler
 * @param [out] ts Tims ms since epoch
 */
IW_EXPORT iwrc iwkv_db_last_access_time(const IWDB db, uint64_t *ts);

/**
 * @brief Destroy(drop) existing database and cleanup all of its data.
 *
 * @param dbp Pointer to database opened.
 */
IW_EXPORT iwrc iwkv_db_destroy(IWDB *dbp);

/**
 * @brief Sync iwkv storage state with disk.
 *
 * @param iwkv Storage handler.
 * @param flags Sync flags.
 */
IW_EXPORT iwrc iwkv_sync(IWKV iwkv, iwfs_sync_flags flags);

/**
 * @brief Close iwkv storage.
 * @details Upon successfull call of iwkv_close()
 * no farther operations on storage or any of its databases are allowed.
 *
 * @param iwkvp
 */
IW_EXPORT iwrc iwkv_close(IWKV *iwkvp);

/**
 * @brief Store record in database.
 *
 * iwkv_opflags opflags:
 * - `IWKV_NO_OVERWRITE` If a key is already exists the `IWKV_ERROR_KEY_EXISTS` error will returned.
 * - `IWKV_SYNC` Flush changes on disk after operation
 * - `IWKV_DUP_REMOVE` Remove value from duplicated values array. Usable only for IWDB_DUP_XXX DB database modes.
 *
 * @note `iwkv_put()` adds a new value to sorted values array for existing keys if
 * database created with `IWDB_DUP_UINT32_VALS`|`IWDB_DUP_UINT64_VALS` flags
 *
 * @param db Database handler
 * @param key Key data container
 * @param val Value data container
 * @param opflags Put options used
 */
IW_EXPORT iwrc iwkv_put(IWDB db, const IWKV_val *key, const IWKV_val *val, iwkv_opflags opflags);


/**
 * @brief Get value for given `key`.
 *
 * @note If not matching record found `IWKV_ERROR_NOTFOUND` will be returned.
 * @note On success a returned value must be freed with `iwkv_val_dispose()`
 *
 * @param db Database handler
 * @param key Key data
 * @param [out] oval Value associated with `key` or `NULL`
 */
IW_EXPORT iwrc iwkv_get(IWDB db, const IWKV_val *key, IWKV_val *oval);

/**
 * @brief Remove record identified by `key`.
 *
 * Returns `IWKV_ERROR_NOTFOUND` is no matching key found
 * @param db Database handler
 * @param key Key data container
 */
IW_EXPORT iwrc iwkv_del(IWDB db, const IWKV_val *key);

/**
 * @brief Destroy key/value data container.
 *
 */
IW_EXPORT void iwkv_val_dispose(IWKV_val *kval);

/**
 * @brief Dispose data containers for key and value respectively.
 *
 * @note This method is shortland of:
 * @code {.c}
 *  iwkv_kv_dispose(key);
 *  iwkv_kv_dispose(val);
 * @endcode
 *
 * @param key Key data containers
 * @param val Value data containers
 */
IW_EXPORT void iwkv_kv_dispose(IWKV_val *key, IWKV_val *val);

/**
 * @brief Open database cursor.
 *
 * @param db Database handler
 * @param cur Pointer to an allocated cursor structure to be initialized
 * @param op Cursor open mode/initial positions flags
 * @param key Optional key argument, required to point cursor to the given key.
 */
IW_EXPORT WUR iwrc iwkv_cursor_open(IWDB db,
                                    IWKV_cursor *cur,
                                    IWKV_cursor_op op,
                                    const IWKV_val *key);
/**
 * @brief Move cursor to the next position.
 *
 * @param cur Opened cursor object
 * @param op Cursor position operation
 */
IW_EXPORT WUR iwrc iwkv_cursor_to(IWKV_cursor cur, IWKV_cursor_op op);

/**
 * @brief Move cursor to the next position.
 *
 * @param cur Opened cursor object
 * @param op Cursor position operation
 * @param key Optional key argument used to move cursor to the given key.
 */
IW_EXPORT WUR iwrc iwkv_cursor_to_key(IWKV_cursor cur, IWKV_cursor_op op, const IWKV_val *key);

/**
 * @brief Get key and value at current cursor position.
 * @note Data stored in okey/oval containers must be freed with `iwkv_val_dispose()`.
 *
 * @param cur Opened cursor object
 * @param okey Key container to be initialized by key at current position. Can be null.
 * @param oval Value container to be initialized by value at current position. Can be null.
 */
IW_EXPORT iwrc iwkv_cursor_get(IWKV_cursor cur, IWKV_val *okey, IWKV_val *oval);

/**
 * @brief Get value at current cursor position.
 * @note Data stored in oval container must be freed with `iwkv_val_dispose()`.
 * @param cur Opened cursor object
 * @param oval Value holder to be initialized by value at current position
 */
IW_EXPORT iwrc iwkv_cursor_val(IWKV_cursor cur, IWKV_val *oval);

/**
 * @brief Copy value data to the specified buffer at the current cursor position.
 * @note At most of `bufsz` bytes will be copied into `vbuf`.
 *
 * @param cur Opened cursor object
 * @param vbuf Pointer to value buffer
 * @param vbufsz Value buffer size
 * @param [out] vsz Actual value size
 */
IW_EXPORT iwrc iwkv_cursor_copy_val(IWKV_cursor cur, uint8_t *vbuf, size_t vbufsz, size_t *vsz);

/**
 * @brief Get key at current cursor position.
 * @note Data stored in okey container must be freed with `iwkv_val_dispose()`.
 *
 * @param cur Opened cursor object
 * @param oval Key holder to be initialized by key at current position
 */
IW_EXPORT iwrc iwkv_cursor_key(IWKV_cursor cur, IWKV_val *okey);

/**
 * @brief Copy key data to the specified buffer at the current cursor position.
 * @note At most of `bufsz` bytes will be copied into `kbuf`.
 *
 * @param cur Opened cursor object
 * @param kbuf Pointer to value buffer
 * @param kbufsz Key buffer size
 * @param [out] ksz Actual key size
 */
IW_EXPORT iwrc iwkv_cursor_copy_key(IWKV_cursor cur, uint8_t *kbuf, size_t kbufsz, size_t *ksz);

/**
 * @brief Set record value at current cursor position.
 * @note This is equivalent to `iwkv_put()` operation.
 *
 * iwkv_opflags opflags:
 * - `IWKV_NO_OVERWRITE` If a key is already exists the `IWKV_ERROR_KEY_EXISTS` error will returned.
 * - `IWKV_SYNC` Flush changes on disk after operation
 * - `IWKV_DUP_REMOVE` Remove value from duplicated values array. Usable only for IWDB_DUP_XXX DB database modes.
 *
 * @note `iwkv_cursor_set()` adds a new value to sorted values array for existing keys if
 * database created with `IWDB_DUP_UINT32_VALS`|`IWDB_DUP_UINT64_VALS` flags
 *
 * @param cur Opened cursor object
 * @param val Value holder
 * @param opflags Update value mode
 */
IW_EXPORT iwrc iwkv_cursor_set(IWKV_cursor cur, IWKV_val *val, iwkv_opflags opflags);

/**
 * @brief Get length of value array at the current cursor position.
 * @note Usable only for `IWDB_DUP_UINT32_VALS`|`IWDB_DUP_UINT64_VALS` database modes.
 *
 * @param cur Opened cursor object
 * @param [out] onum Output number
 */
IW_EXPORT iwrc iwkv_cursor_dup_num(const IWKV_cursor cur, uint32_t *onum);

/**
 * @brief Add element to value array at current cursor position.
 * @note Usable only for `IWDB_DUP_UINT32_VALS`|`IWDB_DUP_UINT64_VALS` database modes.
 *
 * @param cur Opened cursor object
 * @param dv Number to be added
 */
IW_EXPORT iwrc iwkv_cursor_dup_add(IWKV_cursor cur, uint64_t dv);

/**
 * @brief Remove element from value array at current cursor position.
 * @note Usable only for `IWDB_DUP_UINT32_VALS`|`IWDB_DUP_UINT64_VALS` database modes.
 *
 * @param cur Opened cursor object
 * @param dv Number to be removed
 */
IW_EXPORT iwrc iwkv_cursor_dup_rm(IWKV_cursor cur, uint64_t dv);

/**
 * @brief Test if given number contains in value array at current cursor position.
 *
 * @param cur Opened cursor object
 * @param dv Value to test
 * @param [out] out Boolean result
 */
IW_EXPORT iwrc iwkv_cursor_dup_contains(const IWKV_cursor cur, uint64_t dv, bool *out);

/**
 * @brief Iterate over all elements in array of numbers at current cursor position.
 *
 * @param cur Opened cursor
 * @param visitor Elements visitor function
 * @param opaq Opaque data passed to visitor function
 * @param start Optional pointer to number iteration will start from
 * @param down Iteration direction
 */
IW_EXPORT iwrc iwkv_cursor_dup_iter(const IWKV_cursor cur,
                                    bool(*visitor)(uint64_t dv, void *opaq),
                                    void *opaq,
                                    const uint64_t *start,
                                    bool down);
/**
 * @brief Close cursor object.
 * @param cur Opened cursor
 */
IW_EXPORT iwrc iwkv_cursor_close(IWKV_cursor *cur);

// Do not print random levels of skiplist blocks
#define IWKVD_PRINT_NO_LEVEVELS 0x1

// Print record values
#define IWKVD_PRINT_VALS 0x2

void iwkvd_db(FILE *f, IWDB db, int flags, int plvl);

IW_EXTERN_C_END

#endif
