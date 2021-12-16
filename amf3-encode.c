/*
** Copyright (C) 2010-2018 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "zend_smart_str.h"
#include "amf3.h"

#define MAXDEPTH 100 /* Arbitrary call depth limit for recursion check */

static void encodeU29(smart_str *ss, int val) {
	char buf[4];
	int len;
	val &= 0x1fffffff;
	if (val <= 0x7f) {
		buf[0] = val;
		len = 1;
	} else if (val <= 0x3fff) {
		buf[0] = (val >> 7) | 0x80;
		buf[1] = val & 0x7f;
		len = 2;
	} else if (val <= 0x1fffff) {
		buf[0] = (val >> 14) | 0x80;
		buf[1] = (val >> 7) | 0x80;
		buf[2] = val & 0x7f;
		len = 3;
	} else {
		buf[0] = (val >> 22) | 0x80;
		buf[1] = (val >> 15) | 0x80;
		buf[2] = (val >> 8) | 0x80;
		buf[3] = val;
		len = 4;
	}
	smart_str_appendl(ss, buf, len);
}

static void encodeDouble(smart_str *ss, double val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	char buf[8];
	t.i = 1;
	u.d = val;
	if (!t.c) memcpy(buf, u.c, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) buf[7 - i] = u.c[i];
	}
	smart_str_appendl(ss, buf, 8);
}

static int encodeRefEx(smart_str *ss, const char *str, size_t len, HashTable *ht) {
	int *oidx, nidx;
	if ((oidx = zend_hash_str_find_ptr(ht, str, len))) {
		encodeU29(ss, *oidx << 1);
		return 1;
	}
	nidx = zend_hash_num_elements(ht);
	if (nidx <= AMF3_INT_MAX) zend_hash_str_add_mem(ht, str, len, &nidx, sizeof nidx);
	return 0;
}

static int encodeRef(smart_str *ss, void *ptr, HashTable *ht) {
	return encodeRefEx(ss, (char *)&ptr, sizeof ptr, ht);
}

static void encodeString(smart_str *ss, const char *str, size_t len, HashTable *ht) {
	if (len > AMF3_INT_MAX) len = AMF3_INT_MAX;
	if (len && encodeRefEx(ss, str, len, ht)) return; /* Empty string is never sent by reference */
	encodeU29(ss, (len << 1) | 1);
	smart_str_appendl(ss, str, len);
}

static void encodeValue(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl);

static void encodeHash(smart_str *ss, HashTable *ht, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl, int obj) {
	zend_ulong idx;
	zend_string *key;
	zval *val;
	ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, val) {
		if (key) {
			const char *str = ZSTR_VAL(key);
			size_t len = ZSTR_LEN(key);
			if (!len) continue; /* Empty key can't be represented in AMF3 */
			if (obj && !str[0]) continue; /* Skip private/protected property */
			encodeString(ss, str, len, sht);
		} else {
			char buf[22];
			encodeString(ss, buf, sprintf(buf, "%ld", idx), sht);
		}
		encodeValue(ss, val, opts, sht, oht, tht, lvl + 1);
	} ZEND_HASH_FOREACH_END();
	smart_str_appendc(ss, 0x01);
}

static void encodeArray(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl, int len) {
	HashTable *ht = HASH_OF(val);
	if (encodeRef(ss, ht, oht)) return;
	if (len != -1) { /* Encode as dense array */
		encodeU29(ss, (len << 1) | 1);
		smart_str_appendc(ss, 0x01);
		ZEND_HASH_FOREACH_VAL(ht, val) {
			encodeValue(ss, val, opts, sht, oht, tht, lvl + 1);
		} ZEND_HASH_FOREACH_END();
	} else { /* Encode as associative array */
		smart_str_appendc(ss, 0x01);
		encodeHash(ss, ht, opts, sht, oht, tht, lvl, 0);
	}
}

static void encodeObject(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl) {
	HashTable *ht = HASH_OF(val);
	zend_class_entry *ce = Z_TYPE_P(val) == IS_OBJECT ? Z_OBJCE_P(val) : zend_standard_class_def;
	int *oidx, nidx;
	if (encodeRef(ss, ht, oht)) return;
	if ((oidx = zend_hash_str_find_ptr(tht, (char *)&ce, sizeof ce))) encodeU29(ss, (*oidx << 2) | 1);
	else {
		nidx = zend_hash_num_elements(tht);
		if (nidx <= AMF3_INT_MAX) zend_hash_str_add_mem(tht, (char *)&ce, sizeof ce, &nidx, sizeof nidx);
		smart_str_appendc(ss, 0x0b);
		if (ce == zend_standard_class_def) smart_str_appendc(ss, 0x01); /* Anonymous object */
		else encodeString(ss, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), sht); /* Typed object */
	}
	encodeHash(ss, ht, opts, sht, oht, tht, lvl, 1);
}

static int getArrayLength(zval *val) {
	int len = 0;
	zend_ulong idx;
	zend_string *key;
	ZEND_HASH_FOREACH_KEY(HASH_OF(val), idx, key) {
		if (key || idx != len || ++len == AMF3_INT_MAX) return -1;
	} ZEND_HASH_FOREACH_END();
	return len;
}

static void encodeValueData(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl) {
	switch (Z_TYPE_P(val)) {
		default:
			smart_str_appendc(ss, AMF3_UNDEFINED);
			break;
		case IS_NULL:
			smart_str_appendc(ss, AMF3_NULL);
			break;
		case IS_FALSE:
			smart_str_appendc(ss, AMF3_FALSE);
			break;
		case IS_TRUE:
			smart_str_appendc(ss, AMF3_TRUE);
			break;
		case IS_LONG: {
			zend_long x = Z_LVAL_P(val);
			if (x >= AMF3_INT_MIN && x <= AMF3_INT_MAX) {
				smart_str_appendc(ss, AMF3_INTEGER);
				encodeU29(ss, x);
			} else {
				smart_str_appendc(ss, AMF3_DOUBLE);
				encodeDouble(ss, x);
			}
			break;
		}
		case IS_DOUBLE:
			smart_str_appendc(ss, AMF3_DOUBLE);
			encodeDouble(ss, Z_DVAL_P(val));
			break;
		case IS_STRING:
			smart_str_appendc(ss, AMF3_STRING);
			encodeString(ss, Z_STRVAL_P(val), Z_STRLEN_P(val), sht);
			break;
		case IS_ARRAY: {
			int len = getArrayLength(val);
			if (!(opts & AMF3_FORCE_OBJECT) || len != -1) {
				smart_str_appendc(ss, AMF3_ARRAY);
				encodeArray(ss, val, opts, sht, oht, tht, lvl, len);
				break;
			}
			/* Fall through; encode array as object */
		}
		case IS_OBJECT:
			smart_str_appendc(ss, AMF3_OBJECT);
			encodeObject(ss, val, opts, sht, oht, tht, lvl);
			break;
		case IS_REFERENCE:
			encodeValue(ss, Z_REFVAL_P(val), opts, sht, oht, tht, lvl);
			break;
	}
}

static void encodeValue(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int lvl) {
	zval func, res;
	if (lvl > MAXDEPTH) zend_error_noreturn(E_ERROR, "Recursion detected");
	if (Z_TYPE_P(val) != IS_OBJECT || !instanceof_function(Z_OBJCE_P(val), amf3_serializable_ce)) {
		encodeValueData(ss, val, opts, sht, oht, tht, lvl);
		return;
	}
	ZVAL_STRING(&func, "__toAMF3");
	call_user_function(0, val, &func, &res, 0, 0);
	zval_ptr_dtor(&func);
	if (EG(exception)) {
		zval_ptr_dtor(&res);
		return;
	}
	encodeValueData(ss, &res, opts, sht, oht, tht, lvl);
	zval_ptr_dtor(&res);
}

static void freePtr(zval *val) {
	efree(Z_PTR_P(val));
}

PHP_FUNCTION(amf3_encode) {
	smart_str ss = {0};
	zval *val;
	zend_long opts = 0;
	HashTable sht, oht, tht;
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|l", &val, &opts) == FAILURE) return;
	zend_hash_init(&sht, 0, 0, freePtr, 0);
	zend_hash_init(&oht, 0, 0, freePtr, 0);
	zend_hash_init(&tht, 0, 0, freePtr, 0);
	encodeValue(&ss, val, opts, &sht, &oht, &tht, 0);
	zend_hash_destroy(&sht);
	zend_hash_destroy(&oht);
	zend_hash_destroy(&tht);
	if (EG(exception)) {
		smart_str_free(&ss);
		return;
	}
	smart_str_0(&ss);
	RETURN_STR(ss.s);
}
