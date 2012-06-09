/*
 * ========================================================================
 * PHP AMF3 encoding/decoding extension
 * Work started by Arseny Vakhrushev on 11 Jan 2010
 * Copyright (C) 2010 IT Territory, LLC. http://it-territory.ru/
 * ========================================================================
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Please read the LICENSE file for license details
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"

static zend_function_entry amf3_functions[] = {
	PHP_FE(amf3_encode, NULL)
	PHP_FE(amf3_decode, NULL)
	{ NULL, NULL, NULL }
};

zend_module_entry amf3_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	PHP_AMF3_WORLD_EXTNAME,
	amf3_functions,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
#if ZEND_MODULE_API_NO >= 20010901
	PHP_AMF3_WORLD_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_AMF3
ZEND_GET_MODULE(amf3)
#endif


/* ============================================================================================================ */

/* Some Zend Framework compatibility macros */
#ifndef Z_ISREF_P
#define Z_ISREF_P(A) PZVAL_IS_REF(A)
#endif
#ifndef Z_ISREF_PP
#define Z_ISREF_PP(A) PZVAL_IS_REF(*(A))
#endif
#ifndef Z_SET_ISREF_P
#define Z_SET_ISREF_P(A) { PZVAL_IS_REF(A) = 1; }
#endif
#ifndef Z_SET_ISREF_PP
#define Z_SET_ISREF_PP(A) Z_SET_ISREF_P(*(A))
#endif
#ifndef Z_ADDREF_P
#define Z_ADDREF_P(A) ZVAL_ADDREF(A)
#endif
#ifndef Z_ADDREF_PP
#define Z_ADDREF_PP(A) Z_ADDREF_P(*(A))
#endif


#define AMF3_MAX_INT     268435455 //  (2^28)-1
#define AMF3_MIN_INT    -268435456 // -(2^28)


typedef enum amf3_type_e amf3_type_t;
typedef struct amf3_chunk_s amf3_chunk_t;
typedef struct amf3_env_s amf3_env_t;

enum amf3_type_e {
	AMF3_UNDEFINED    = 0x00,
	AMF3_NULL         = 0x01,
	AMF3_FALSE        = 0x02,
	AMF3_TRUE         = 0x03,
	AMF3_INTEGER      = 0x04,
	AMF3_DOUBLE       = 0x05,
	AMF3_STRING       = 0x06,
	AMF3_XMLDOC       = 0x07, // no support
	AMF3_DATE         = 0x08, // [TODO]
	AMF3_ARRAY        = 0x09,
	AMF3_OBJECT       = 0x0a, // no support
	AMF3_XML          = 0x0b, // no support
	AMF3_BYTEARRAY    = 0x0c, // [TODO]
};

struct amf3_chunk_s {
	char         buf[1000]; // some suitable chunk size to store both small and big buffers well enough and to fit 1Kb memory block
	int          size;
	amf3_chunk_t *next;
};

struct amf3_env_s {
	HashTable    strs;
	HashTable    objs;
};


/* ============================================================================================================ */


amf3_chunk_t *amf3_initChunk(amf3_chunk_t *chunk) {
	if (!chunk) chunk = malloc(sizeof(*chunk));
	if (!chunk) return NULL;
	memset(chunk, 0, sizeof(*chunk));
	return chunk;
}

amf3_chunk_t *amf3_appendChunk(amf3_chunk_t *chunk, char *buf, int size) {
	if (!chunk) return NULL;
	for ( ;; ) {
		int avail = sizeof(chunk->buf) - chunk->size;
		if (avail >= size) break;
		memcpy(chunk->buf + chunk->size, buf, avail);
		chunk->size += avail;
		buf += avail;
		size -= avail;
		if (!chunk->next) chunk->next = amf3_initChunk(NULL);
		chunk = chunk->next;
	}
	memcpy(chunk->buf + chunk->size, buf, size);
	chunk->size += size;
	return chunk;
}

void amf3_freeChunk(amf3_chunk_t *chunk, char *buf) {
	amf3_chunk_t *next;
	while (chunk) {
		next = chunk->next;
		memcpy(buf, chunk->buf, chunk->size);
		buf += chunk->size;
		free(chunk);
		chunk = next;
	}
}

int amf3_getStrIdx(amf3_env_t *env, char *str, int len) {
	if (len <= 0) return -1; // empty string is never sent by reference
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	int *oldIdx;
	if (zend_hash_find(&env->strs, (char *)str, len, (void **)&oldIdx) == SUCCESS) return *oldIdx;
	int newIdx = zend_hash_num_elements(&env->strs);
	if (newIdx <= AMF3_MAX_INT) zend_hash_add(&env->strs, (char *)str, len, &newIdx, sizeof(newIdx), NULL);
	return -1;
}

int amf3_getObjIdx(amf3_env_t *env, zval *val) {
	int *oldIdx;
	if (Z_ISREF_P(val) && (zend_hash_find(&env->objs, (char *)&val, sizeof(val), (void **)&oldIdx) == SUCCESS)) return *oldIdx;
	int newIdx = zend_hash_num_elements(&env->objs);
	if (newIdx <= AMF3_MAX_INT) zend_hash_add(&env->objs, (char *)&val, sizeof(val), &newIdx, sizeof(newIdx), NULL);
	return -1;
}

zval *amf3_getRef(HashTable *ht, int idx) {
	zval **val;
	if (zend_hash_index_find(ht, idx, (void **)&val) == FAILURE) return NULL;
	Z_ADDREF_PP(val);
	return *val;
}

void amf3_putRef(HashTable *ht, zval *val) {
	Z_ADDREF_P(val);
	zend_hash_index_update(ht, zend_hash_num_elements(ht), &val, sizeof(val), NULL);
}


/* ============================================================================================================ */


int amf3_encodeChar(amf3_chunk_t **chunk, char c) {
	*chunk = amf3_appendChunk(*chunk, &c, 1);
	return 1;
}

int amf3_encodeU29(amf3_chunk_t **chunk, int val) {
	char buf[4];
	int pos;
	val &= 0x1fffffff;
	if (val <= 0x7f) {
		buf[0] = val;
		pos = 1;
	} else if (val <= 0x3fff) {
		buf[1] = val & 0x7f;
		val >>= 7;
		buf[0] = val | 0x80;
		pos = 2;
	} else if (val <= 0x1fffff) {
		buf[2] = val & 0x7f;
		val >>= 7;
		buf[1] = val | 0x80;
		val >>= 7;
		buf[0] = val | 0x80;
		pos = 3;
	} else {
		buf[3] = val;
		val >>= 8;
		buf[2] = val | 0x80;
		val >>= 7;
		buf[1] = val | 0x80;
		val >>= 7;
		buf[0] = val | 0x80;
		pos = 4;
	}
	*chunk = amf3_appendChunk(*chunk, buf, pos);
	return pos;
}

int amf3_decodeU29(int *val, char *buf, int size) {
	int pos = 0, res = 0, tmp;
	do {
		if (pos >= size) return -1;
		tmp = buf[pos];
		if (pos == 3) {
			res <<= 8;
			res |= tmp & 0xff;
		} else {
			res <<= 7;
			res |= tmp & 0x7f;
		}
	} while ((++pos < 4) && (tmp & 0x80));
	*val = res;
	return pos;
}

int amf3_encodeDouble(amf3_chunk_t **chunk, double val) {
	union {
		double d;
		int64_t l;
	} u = { val };
	int64_t l = u.l;
	char buf[8];
	int i;
	for (i = 0; i < 8; ++i) {
		buf[7 - i] = l;
		l >>= 8;
	}
	*chunk = amf3_appendChunk(*chunk, buf, 8);
	return 8;
}

int amf3_decodeDouble(double *val, char *buf, int size) {
	if (size < 8) return -1;
	int64_t l = 0;
	int i;
	for (i = 0; i < 8; ++i) {
		l <<= 8;
		l |= buf[i] & 0xff;
	}
	union {
		int64_t l;
		double d;
	} u = { l };
	*val = u.d;
	return 8;
}

int amf3_encodeStr(amf3_chunk_t **chunk, char *str, int len, amf3_env_t *env TSRMLS_DC) {
	int pos = 0, idx = amf3_getStrIdx(env, str, len);
	if (idx >= 0) pos += amf3_encodeU29(chunk, idx << 1); // encode as a reference
	else {
		if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
		pos += amf3_encodeU29(chunk, (len << 1) | 1) + len;
		*chunk = amf3_appendChunk(*chunk, str, len);
	}
	return pos;
}

int amf3_decodeStr(char **str, int *len, char *buf, int size, amf3_env_t *env TSRMLS_DC) {
	int pfx, pos = amf3_decodeU29(&pfx, buf, size);
	if (pos < 0) return -1;
	if (!(pfx & 1)) { // decode as a reference
		zval **val;
		if (zend_hash_index_find(&env->strs, pfx >> 1, (void **)&val) == FAILURE) return -1;
		*str = Z_STRVAL_PP(val);
		*len = Z_STRLEN_PP(val);
	} else {
		pfx >>= 1;
		if ((pfx < 0) || ((pos + pfx) > size)) return -1;
		*str = buf + pos;
		*len = pfx;
		if (pfx > 0) { // empty string is never sent by reference
			zval *val;
			ALLOC_INIT_ZVAL(val);
			ZVAL_STRINGL(val, buf + pos, pfx, 1);
			zend_hash_index_update(&env->strs, zend_hash_num_elements(&env->strs), &val, sizeof(val), NULL);
		}
		pos += pfx;
	}
	return pos;
}

int amf3_encodeVal(amf3_chunk_t **chunk, zval *val, amf3_env_t *env TSRMLS_DC) {
	int pos = 0;
	switch (Z_TYPE_P(val)) {
		default:
			pos += amf3_encodeChar(chunk, AMF3_UNDEFINED);
			break;
		case IS_NULL:
			pos += amf3_encodeChar(chunk, AMF3_NULL);
			break;
		case IS_BOOL:
			pos += amf3_encodeChar(chunk, Z_LVAL_P(val) ? AMF3_TRUE : AMF3_FALSE);
			break;
		case IS_LONG:
			if ((Z_LVAL_P(val) < AMF3_MIN_INT) || (Z_LVAL_P(val) > AMF3_MAX_INT)) {
				pos += amf3_encodeChar(chunk, AMF3_DOUBLE);
				pos += amf3_encodeDouble(chunk, Z_LVAL_P(val));
			} else {
				pos += amf3_encodeChar(chunk, AMF3_INTEGER);
				pos += amf3_encodeU29(chunk, Z_LVAL_P(val));
			}
			break;
		case IS_DOUBLE:
			pos += amf3_encodeChar(chunk, AMF3_DOUBLE);
			pos += amf3_encodeDouble(chunk, Z_DVAL_P(val));
			break;
		case IS_STRING:
			pos += amf3_encodeChar(chunk, AMF3_STRING);
			pos += amf3_encodeStr(chunk, Z_STRVAL_P(val), Z_STRLEN_P(val), env TSRMLS_CC);
			break;
		case IS_ARRAY: {
			pos += amf3_encodeChar(chunk, AMF3_ARRAY);
			int idx = amf3_getObjIdx(env, val);
			if (idx >= 0) pos += amf3_encodeU29(chunk, idx << 1); // encode as a reference
			else {
				HashTable *ht = Z_ARRVAL_P(val);
				HashPosition hp;
				zval **hv;
				char *key, keyBuf[22];
				int keyType;
				uint keyLen;
				ulong idx, num = 0;
				for (zend_hash_internal_pointer_reset_ex(ht, &hp);; zend_hash_move_forward_ex(ht, &hp)) {
					keyType = zend_hash_get_current_key_ex(ht, &key, &keyLen, &idx, 0, &hp);
					if ((keyType != HASH_KEY_IS_LONG) || (idx != num)) break;
					++num;
				}
				if (num == zend_hash_num_elements(ht)) { // sequence of values with integer indexes starting from zero
					if (num > AMF3_MAX_INT) num = AMF3_MAX_INT;
					pos += amf3_encodeU29(chunk, (num << 1) | 1); // dense part size
					pos += amf3_encodeChar(chunk, 0x01); // end of associative part
					for (zend_hash_internal_pointer_reset_ex(ht, &hp); (num-- > 0) && (zend_hash_get_current_data_ex(ht, (void **)&hv, &hp) == SUCCESS); zend_hash_move_forward_ex(ht, &hp)) {
						pos += amf3_encodeVal(chunk, *hv, env TSRMLS_CC);
					}
				} else { // associative array with mixed keys
					pos += amf3_encodeChar(chunk, 0x01); // empty dense part
					for (zend_hash_internal_pointer_reset_ex(ht, &hp); zend_hash_get_current_data_ex(ht, (void **)&hv, &hp) == SUCCESS; zend_hash_move_forward_ex(ht, &hp)) {
						keyType = zend_hash_get_current_key_ex(ht, &key, &keyLen, &idx, 0, &hp);
						if (keyType == HASH_KEY_IS_STRING) {
							if (keyLen <= 1) continue; // empty keys can't be represented in AMF3
							pos += amf3_encodeStr(chunk, key, keyLen - 1, env TSRMLS_CC);
						} else if (keyType == HASH_KEY_IS_LONG) {
							keyLen = sprintf(keyBuf, "%ld", idx);
							pos += amf3_encodeStr(chunk, keyBuf, keyLen, env TSRMLS_CC);
						} else continue;
						pos += amf3_encodeVal(chunk, *hv, env TSRMLS_CC);
					}
					pos += amf3_encodeChar(chunk, 0x01); // end of associative part
				}
			}
			break;
		}
	}
	return pos;
}

void amf3_initVal(zval **val) {
	if (*val) {
		zval_dtor(*val);
		ZVAL_NULL(*val);
	} else {
		ALLOC_INIT_ZVAL(*val);
	}
}

int amf3_decodeVal(zval **val, char *data, int pos, int size, amf3_env_t *env TSRMLS_DC) {
	if ((pos < 0) || (pos >= size)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode type specifier at position %d", pos);
		return -1;
	}
	int oldPos = pos;
	amf3_type_t type = (unsigned char)data[pos++];
	switch (type) {
		case AMF3_UNDEFINED:
		case AMF3_NULL:
			amf3_initVal(val);
			ZVAL_NULL(*val);
			break;
		case AMF3_FALSE:
			amf3_initVal(val);
			ZVAL_FALSE(*val);
			break;
		case AMF3_TRUE:
			amf3_initVal(val);
			ZVAL_TRUE(*val);
			break;
		case AMF3_INTEGER: {
			int i;
			int res = amf3_decodeU29(&i, data + pos, size - pos);
			if (res < 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode integer at position %d", pos);
				return -1;
			}
			if (i & 0x10000000) i |= ~0x1fffffff; // prolong sign bits if negative
			amf3_initVal(val);
			ZVAL_LONG(*val, i);
			pos += res;
			break;
		}
		case AMF3_DOUBLE: {
			double d;
			int res = amf3_decodeDouble(&d, data + pos, size - pos);
			if (res < 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode double at position %d", pos);
				return -1;
			}
			amf3_initVal(val);
			ZVAL_DOUBLE(*val, d);
			pos += res;
			break;
		}
		case AMF3_STRING: {
			int pfx, res = amf3_decodeU29(&pfx, data + pos, size - pos);
			if (res < 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode string prefix at position %d", pos);
				return -1;
			}
			pos += res;
			if (!(pfx & 1)) { // decode as a reference
				*val = amf3_getRef(&env->strs, pfx >> 1);
				if (!*val) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Missing string reference index at position %d", pos - res);
					return -1;
				}
			} else {
				pfx >>= 1;
				if ((pfx < 0) || ((pos + pfx) > size)) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid string length at position %d", pos - res);
					return -1;
				}
				amf3_initVal(val);
				ZVAL_STRINGL(*val, data + pos, pfx, 1);
				pos += pfx;
				if (pfx > 0) amf3_putRef(&env->strs, *val); // empty string is never sent by reference
			}
			break;
		}
		case AMF3_DATE: {
			int pfx, res = amf3_decodeU29(&pfx, data + pos, size - pos);
			if (res < 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode date prefix at position %d", pos);
				return -1;
			}
			pos += res;
			if (!(pfx & 1)) { // decode as a reference
				*val = amf3_getRef(&env->objs, pfx >> 1);
				if (!*val) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Missing date reference index at position %d", pos - res);
					return -1;
				}
				Z_SET_ISREF_PP(val);
			} else {
				double d;
				res = amf3_decodeDouble(&d, data + pos, size - pos);
				if (res < 0) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode date at position %d", pos);
					return -1;
				}
				amf3_initVal(val);
				ZVAL_DOUBLE(*val, d);
				amf3_putRef(&env->objs, *val);
				pos += res;
			}
			break;
		}
		case AMF3_ARRAY: {
			int pfx, res = amf3_decodeU29(&pfx, data + pos, size - pos);
			if (res < 0) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode array prefix at position %d", pos);
				return -1;
			}
			pos += res;
			if (!(pfx & 1)) { // decode as a reference
				*val = amf3_getRef(&env->objs, pfx >> 1);
				if (!*val) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Missing array reference index at position %d", pos - res);
					return -1;
				}
				Z_SET_ISREF_PP(val);
			} else {
				pfx >>= 1;
				if ((pfx < 0) || ((pos + pfx) > size)) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid dense array portion size at position %d", pos - res);
					return -1;
				}
				amf3_initVal(val);
				array_init(*val);
				amf3_putRef(&env->objs, *val);
				char *key, keyBuf[64];
				int keyLen;
				zval *hv;
				for ( ;; ) { // associative array portion
					res = amf3_decodeStr(&key, &keyLen, data + pos, size - pos, env TSRMLS_CC);
					if (res < 0) {
						php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't decode array key at position %d", pos);
						return -1;
					}
					pos += res;
					if (!keyLen) break;
					hv = 0;
					res = amf3_decodeVal(&hv, data, pos, size, env TSRMLS_CC);
					if (hv) { // need a trailing \0 in the key buffer to do a proper call to 'add_assoc_zval_ex'
						if (keyLen < sizeof(keyBuf)) {
							memcpy(keyBuf, key, keyLen);
							keyBuf[keyLen] = 0;
							add_assoc_zval_ex(*val, keyBuf, keyLen + 1, hv);
						} else {
							char *tmpBuf = malloc(keyLen + 1);
							memcpy(tmpBuf, key, keyLen);
							tmpBuf[keyLen] = 0;
							add_assoc_zval_ex(*val, tmpBuf, keyLen + 1, hv);
							free(tmpBuf);
						}
					}
					if (res < 0) return -1; // nested error
					pos += res;
				}
				while (pfx-- > 0) {
					hv = 0;
					res = amf3_decodeVal(&hv, data, pos, size, env TSRMLS_CC);
					if (hv) add_next_index_zval(*val, hv);
					if (res < 0) return -1; // nested error
					pos += res;
				}
			}
			break;
		}
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unsupported value type 0x%02X at position %d", type, pos - 1);
			return -1;
	}
	return pos - oldPos;
}

void amf3_initEnv(amf3_env_t *env, int dtor) {
	memset(env, 0, sizeof(*env));
	zend_hash_init(&env->strs, 10, NULL, dtor ? ZVAL_PTR_DTOR : NULL, 0);
	zend_hash_init(&env->objs, 10, NULL, dtor ? ZVAL_PTR_DTOR : NULL, 0);
}

void amf3_destroyEnv(amf3_env_t *env) {
	zend_hash_destroy(&env->strs);
	zend_hash_destroy(&env->objs);
}


/* ============================================================================================================ */


PHP_FUNCTION(amf3_encode) { // string amf3_encode(mixed value)
	zval *val;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &val) == FAILURE) RETURN_FALSE;
	amf3_chunk_t *begin = amf3_initChunk(NULL);
	amf3_chunk_t *current = begin;
	amf3_env_t env;
	amf3_initEnv(&env, 0);
	int size = amf3_encodeVal(&current, val, &env TSRMLS_CC);
	amf3_destroyEnv(&env);
	char *buf = emalloc(size + 1);
	amf3_freeChunk(begin, buf);
	buf[size] = 0;
	RETURN_STRINGL(buf, size, 0);
}

PHP_FUNCTION(amf3_decode) { // mixed amf3_decode(string data [, int &count])
	char *data;
	int size;
	zval *count = 0;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|z", &data, &size, &count) == FAILURE) RETURN_FALSE;
	amf3_env_t env;
	amf3_initEnv(&env, 1);
	int res = amf3_decodeVal(&return_value, data, 0, size, &env TSRMLS_CC);
	amf3_destroyEnv(&env);
	if (count) {
		zval_dtor(count);
		ZVAL_LONG(count, res);
	}
	if (res < 0) {
		zval_dtor(return_value);
		ZVAL_NULL(return_value);
	}
}
