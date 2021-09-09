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
#include "zend_interfaces.h"
#include "amf3.h"

/* For PHP 7.0 and 7.1 */
#ifndef HT_ALLOW_COW_VIOLATION
#define HT_ALLOW_COW_VIOLATION(ht)
#endif

typedef struct {
	int fmt, cnt;
	zend_string *cls;
	const char **fld;
	int *flen;
} Traits;

static size_t decodeByte(const char *buf, size_t pos, size_t size, int *val) {
	if (pos >= size) {
		php_error(E_WARNING, "Insufficient data at position %zu", pos);
		return 0;
	}
	*val = buf[pos] & 0xff;
	return pos + 1;
}

static size_t decodeU29(const char *buf, size_t pos, size_t size, int *val) {
	int len = 0, x = 0;
	unsigned char c;
	buf += pos;
	do {
		if (pos + len >= size) {
			php_error(E_WARNING, "Insufficient U29 data at position %zu", pos);
			return 0;
		}
		c = buf[len++];
		if (len == 4) {
			x <<= 8;
			x |= c;
			break;
		}
		x <<= 7;
		x |= c & 0x7f;
	} while (c & 0x80);
	*val = x;
	return pos + len;
}

static size_t decodeInteger(const char *buf, size_t pos, size_t size, zval *val) {
	int x;
	pos = decodeU29(buf, pos, size, &x);
	if (!pos) return 0;
	if (x & 0x10000000) x -= 0x20000000;
	ZVAL_LONG(val, x);
	return pos;
}

static size_t decodeU32(const char *buf, size_t pos, size_t size, zval *val, int sign) {
	union { int i; char c; } t;
	union { unsigned u; char c[4]; } u;
	long x;
	if (pos + 4 > size) {
		php_error(E_WARNING, "Insufficient U32 data at position %zu", pos);
		return 0;
	}
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 4);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 4; ++i) u.c[i] = buf[3 - i];
	}
	if (sign) x = (signed)u.u;
	else x = u.u;
	ZVAL_LONG(val, x);
	return pos + 4;
}

static size_t decodeDouble(const char *buf, size_t pos, size_t size, zval *val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	if (pos + 8 > size) {
		php_error(E_WARNING, "Insufficient IEEE-754 data at position %zu", pos);
		return 0;
	}
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	ZVAL_DOUBLE(val, u.d);
	return pos + 8;
}

static size_t decodeString(const char *buf, size_t pos, size_t size, zval *val, const char **str, int *len, HashTable *ht, int blob) {
	int pfx, def;
	size_t _pos = pos;
	pos = decodeU29(buf, pos, size, &pfx);
	if (!pos) return 0;
	def = pfx & 1;
	pfx >>= 1;
	if (def) {
		if (pos + pfx > size) {
			php_error(E_WARNING, "Insufficient data of length %d at position %zu", pfx, pos);
			return 0;
		}
		buf += pos;
		pos += pfx;
		if (val) ZVAL_STRINGL(val, buf, pfx);
		else {
			*str = buf;
			*len = pfx;
		}
		if (blob || pfx) { /* Empty string is never sent by reference */
			zval hv;
			if (val) ZVAL_COPY(&hv, val);
			else ZVAL_STRINGL(&hv, buf, pfx);
			zend_hash_next_index_insert(ht, &hv);
		}
	} else {
		zval *hv;
		if (!(hv = zend_hash_index_find(ht, pfx))) {
			php_error(E_WARNING, "Invalid reference %d at position %zu", pfx, _pos);
			return 0;
		}
		if (val) ZVAL_COPY(val, hv);
		else {
			*str = Z_STRVAL_P(hv);
			*len = Z_STRLEN_P(hv);
		}
	}
	return pos;
}

static size_t decodeRef(const char *buf, size_t pos, size_t size, int *num, zval *val, HashTable *ht) {
	int pfx, def;
	size_t _pos = pos;
	pos = decodeU29(buf, pos, size, &pfx);
	if (!pos) return 0;
	def = pfx & 1;
	pfx >>= 1;
	if (def) *num = pfx;
	else {
		zval *hv;
		if (!(hv = zend_hash_index_find(ht, pfx))) {
			php_error(E_WARNING, "Invalid reference %d at position %zu", pfx, _pos);
			return 0;
		}
		*num = -1;
		ZVAL_COPY(val, hv);
	}
	return pos;
}

static void storeRef(zval *val, HashTable *ht) {
	zval hv;
	ZVAL_NEW_REF(&hv, val);
	Z_TRY_ADDREF_P(val);
	zend_hash_next_index_insert(ht, &hv);
}

static size_t decodeDate(const char *buf, size_t pos, size_t size, zval *val, HashTable *ht) {
	int pfx;
	pos = decodeRef(buf, pos, size, &pfx, val, ht);
	if (!pos) return 0;
	if (pfx != -1) {
		pos = decodeDouble(buf, pos, size, val);
		if (!pos) return 0;
		storeRef(val, ht);
	}
	return pos;
}

static zval *newHashIdx(zval *val) {
	zval hv;
	ZVAL_UNDEF(&hv);
	HT_ALLOW_COW_VIOLATION(HASH_OF(val)); /* PHP DEBUG: suppress reference counter check */
	return zend_hash_next_index_insert(HASH_OF(val), &hv);
}

static zval *newHashKey(zval *val, const char *key, size_t len) {
	zval hv;
	ZVAL_UNDEF(&hv);
	HT_ALLOW_COW_VIOLATION(HASH_OF(val)); /* PHP DEBUG: suppress reference counter check */
	return zend_symtable_str_update(HASH_OF(val), key, len, &hv);
}

static size_t decodeValue(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht);

static size_t decodeArray(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
	int len;
	pos = decodeRef(buf, pos, size, &len, val, oht);
	if (!pos) return 0;
	if (len != -1) {
		const char *key;
		int klen;
		array_init(val);
		storeRef(val, oht);
		for (;;) { /* Associative portion */
			pos = decodeString(buf, pos, size, 0, &key, &klen, sht, 0);
			if (!pos) return 0;
			if (!klen) break;
			pos = decodeValue(buf, pos, size, newHashKey(val, key, klen), opts, sht, oht, tht);
			if (!pos) return 0;
		}
		while (len--) { /* Dense portion */
			pos = decodeValue(buf, pos, size, newHashIdx(val), opts, sht, oht, tht);
			if (!pos) return 0;
		}
	}
	return pos;
}

static size_t decodeObject(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
	int pfx;
	size_t _pos = pos;
	pos = decodeRef(buf, pos, size, &pfx, val, oht);
	if (!pos) return 0;
	if (pfx != -1) {
		int map = opts & AMF3_CLASS_MAP;
		zend_class_entry *ce = 0;
		Traits *tr;
		const char *key;
		int klen;
		int def = pfx & 1;
		pfx >>= 1;
		if (def) { /* New class definition */
			int i, n = pfx >> 2;
			const char *cls;
			int clen;
			const char **fld = 0;
			int *flen = 0;
			pos = decodeString(buf, pos, size, 0, &cls, &clen, sht, 0); /* Class name */
			if (!pos) return 0;
			if (n > 0) {
				if (pos + n > size) {
					php_error(E_WARNING, "Invalid number of class members %d at position %zu", n, _pos);
					return 0;
				}
				fld = emalloc(n * sizeof *fld);
				flen = emalloc(n * sizeof *flen);
				for (i = 0; i < n; ++i) { /* Static member names */
					size_t __pos = pos;
					pos = decodeString(buf, pos, size, 0, &key, &klen, sht, 0);
					if (!pos) {
						n = -1;
						break;
					}
					if (!klen || !key[0]) {
						php_error(E_WARNING, "Invalid class member name at position %zu", __pos);
						n = -1;
						break;
					}
					fld[i] = key;
					flen[i] = klen;
				}
				if (n < 0) {
					efree(fld);
					efree(flen);
					return 0;
				}
			}
			tr = emalloc(sizeof *tr);
			tr->fmt = pfx & 3;
			tr->cnt = n;
			tr->cls = clen ? zend_string_init(cls, clen, 0) : 0;
			tr->fld = fld;
			tr->flen = flen;
			zend_hash_next_index_insert_ptr(tht, tr);
		} else if (!(tr = zend_hash_index_find_ptr(tht, pfx))) { /* Existing class definition */
			php_error(E_WARNING, "Invalid class reference %d at position %zu", pfx, _pos);
			return 0;
		}
		if (!map) array_init(val);
		else {
			if (!tr->cls) object_init(val);
			else {
				int mode = ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_SILENT;
				if (!(opts & AMF3_CLASS_AUTOLOAD)) mode |= ZEND_FETCH_CLASS_NO_AUTOLOAD;
				ce = zend_fetch_class(tr->cls, mode);
				if (!ce) {
					php_error(E_WARNING, "Unknown class '%s' at position %zu", ZSTR_VAL(tr->cls), _pos);
					return 0;
				}
				object_init_ex(val, ce);
			}
		}
		storeRef(val, oht);
		if (tr->fmt & 1) { /* Externalizable */
			pos = decodeValue(buf, pos, size, newHashKey(val, "__data", sizeof "__data" - 1), opts, sht, oht, tht);
			if (!pos) return 0;
		} else {
			int i;
			for (i = 0; i < tr->cnt; ++i) {
				pos = decodeValue(buf, pos, size, newHashKey(val, tr->fld[i], tr->flen[i]), opts, sht, oht, tht);
				if (!pos) return 0;
			}
			if (tr->fmt & 2) { /* Dynamic */
				for (;;) {
					size_t __pos = pos;
					pos = decodeString(buf, pos, size, 0, &key, &klen, sht, 0);
					if (!pos) return 0;
					if (!klen) break;
					if (map && !key[0]) {
						php_error(E_WARNING, "Invalid class member name at position %zu", __pos);
						return 0;
					}
					pos = decodeValue(buf, pos, size, newHashKey(val, key, klen), opts, sht, oht, tht);
					if (!pos) return 0;
				}
			}
		}
		if (!map && tr->cls) {
			HT_ALLOW_COW_VIOLATION(HASH_OF(val)); /* PHP DEBUG: suppress reference counter check */
			add_assoc_stringl(val, "__class", ZSTR_VAL(tr->cls), ZSTR_LEN(tr->cls));
		} else if (ce && (opts & AMF3_CLASS_CONSTRUCT)) { /* Call the constructor */
			zend_call_method_with_0_params(Z_OBJ_P(val), ce, &ce->constructor, NULL, NULL);
			if (EG(exception)) return 0;
		}
	}
	return pos;
}

static int decodeVectorItem(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type) {
	switch (type) {
		case AMF3_VECTOR_INT:
			return decodeU32(buf, pos, size, val, 1);
		case AMF3_VECTOR_UINT:
			return decodeU32(buf, pos, size, val, 0);
		case AMF3_VECTOR_DOUBLE:
			return decodeDouble(buf, pos, size, val);
		default:
			return decodeValue(buf, pos, size, val, opts, sht, oht, tht);
	}
}

static size_t decodeVector(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type) {
	int len;
	pos = decodeRef(buf, pos, size, &len, val, oht);
	if (!pos) return 0;
	if (len != -1) {
		int fv;
		pos = decodeByte(buf, pos, size, &fv); /* 'fixed-vector' marker */
		if (!pos) return 0;
		if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
			const char *ot;
			int otl;
			pos = decodeString(buf, pos, size, 0, &ot, &otl, sht, 0);
			if (!pos) return 0;
		}
		array_init(val);
		storeRef(val, oht);
		while (len--) {
			pos = decodeVectorItem(buf, pos, size, newHashIdx(val), opts, sht, oht, tht, type);
			if (!pos) return 0;
		}
	}
	return pos;
}

static size_t decodeDictionary(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
	/* No support for dictionary in PHP */
	php_error(E_WARNING, "Unsupported 'Dictionary' value at position %zu", pos);
	return 0;
}

static size_t decodeValue(const char *buf, size_t pos, size_t size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
	int type;
	size_t _pos = pos;
	pos = decodeByte(buf, pos, size, &type);
	if (!pos) return 0;
	switch (type) {
		case AMF3_UNDEFINED:
		case AMF3_NULL:
			ZVAL_NULL(val);
			break;
		case AMF3_FALSE:
			ZVAL_FALSE(val);
			break;
		case AMF3_TRUE:
			ZVAL_TRUE(val);
			break;
		case AMF3_INTEGER:
			return decodeInteger(buf, pos, size, val);
		case AMF3_DOUBLE:
			return decodeDouble(buf, pos, size, val);
		case AMF3_STRING:
			return decodeString(buf, pos, size, val, 0, 0, sht, 0);
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			return decodeString(buf, pos, size, val, 0, 0, oht, 1);
		case AMF3_DATE:
			return decodeDate(buf, pos, size, val, oht);
		case AMF3_ARRAY:
			return decodeArray(buf, pos, size, val, opts, sht, oht, tht);
		case AMF3_OBJECT:
			return decodeObject(buf, pos, size, val, opts, sht, oht, tht);
		case AMF3_VECTOR_INT:
		case AMF3_VECTOR_UINT:
		case AMF3_VECTOR_DOUBLE:
		case AMF3_VECTOR_OBJECT:
			return decodeVector(buf, pos, size, val, opts, sht, oht, tht, type);
		case AMF3_DICTIONARY:
			return decodeDictionary(buf, pos, size, val, opts, sht, oht, tht);
		default:
			php_error(E_WARNING, "Invalid value type %d at position %zu", type, _pos);
			return 0;
	}
	return pos;
}

static void freeTraits(zval *val) {
	Traits *tr = Z_PTR_P(val);
	if (tr->cls) zend_string_release(tr->cls);
	efree(tr->fld);
	efree(tr->flen);
	efree(tr);
}

PHP_FUNCTION(amf3_decode) {
	const char *buf;
	size_t size, pos = 0;
	zval *pval = 0;
	zend_long opts = 0;
	HashTable sht, oht, tht;
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|z/l", &buf, &size, &pval, &opts) == FAILURE) return;
	if (pval) {
		if (Z_TYPE_P(pval) == IS_LONG) {
			pos = Z_LVAL_P(pval);
			if (pos > size) {
				php_error(E_WARNING, "Position out of range");
				ZVAL_LONG(pval, -1);
				return;
			}
		}
		zval_ptr_dtor(pval);
	}
	zend_hash_init(&sht, 0, 0, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&oht, 0, 0, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&tht, 0, 0, freeTraits, 0);
	pos = decodeValue(buf, pos, size, return_value, opts, &sht, &oht, &tht);
	zend_hash_destroy(&sht);
	zend_hash_destroy(&oht);
	zend_hash_destroy(&tht);
	if (pval) ZVAL_LONG(pval, pos ? pos : -1);
	if (pos) return;
	zval_ptr_dtor(return_value);
	ZVAL_NULL(return_value);
}
