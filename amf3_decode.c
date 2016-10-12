/*
** Copyright (C) 2010, 2013-2016 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "zend_interfaces.h"
#include "amf3.h"


typedef struct {
	int fmt, cnt;
	zend_string *cls;
	const char **fld;
	int *flen;
} Traits;


static int decodeValue(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC);

static int decodeU8(const char *buf, int pos, int size, unsigned char *val TSRMLS_DC) {
	if (pos >= size) {
		php_error(E_WARNING, "Insufficient U8 data at position %d", pos);
		return -1;
	}
	*val = buf[pos];
	return 1;
}

static int decodeU29(const char *buf, int pos, int size, int *val TSRMLS_DC) {
	int n = 0, ofs = 0;
	unsigned char b;
	buf += pos;
	do {
		if ((pos + ofs) >= size) {
			php_error(E_WARNING, "Insufficient U29 data at position %d", pos);
			return -1;
		}
		b = buf[ofs++];
		if (ofs == 4) {
			n <<= 8;
			n |= b;
			break;
		}
		n <<= 7;
		n |= b & 0x7f;
	} while (b & 0x80);
	*val = n;
	return ofs;
}

static int decodeInteger(const char *buf, int pos, int size, zval *val TSRMLS_DC) {
	int n, ofs = decodeU29(buf, pos, size, &n TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (n & 0x10000000) n -= 0x20000000;
	ZVAL_LONG(val, n);
	return ofs;
}

static int decodeU32(const char *buf, int pos, int size, zval *val, int sign TSRMLS_DC) {
	union { int n; char c; } t;
	union { unsigned n; char c[4]; } u;
	long n;
	if ((pos + 4) > size) {
		php_error(E_WARNING, "Insufficient U32 data at position %d", pos);
		return -1;
	}
	buf += pos;
	t.n = 1;
	if (!t.c) memcpy(u.c, buf, 4);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 4; ++i) u.c[i] = buf[3 - i];
	}
	if (sign) n = (signed)u.n;
	else n = u.n;
	ZVAL_LONG(val, n);
	return 4;
}

static int decodeDouble(const char *buf, int pos, int size, zval *val TSRMLS_DC) {
	union { int n; char c; } t;
	union { double d; char c[8]; } u;
	if ((pos + 8) > size) {
		php_error(E_WARNING, "Insufficient IEEE-754 data at position %d", pos);
		return -1;
	}
	buf += pos;
	t.n = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	ZVAL_DOUBLE(val, u.d);
	return 8;
}

static int decodeString(const char *buf, int pos, int size, zval *val, const char **str, int *len, HashTable *ht, int blob TSRMLS_DC) {
	int old = pos, ofs, pfx, def;
	ofs = decodeU29(buf, pos, size, &pfx TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) {
		if ((pos + pfx) > size) {
			php_error(E_WARNING, "Invalid string length %d at position %d", pfx, old);
			return -1;
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
			php_error(E_WARNING, "Invalid string reference %d at position %d", pfx, old);
			return -1;
		}
		if (val) ZVAL_COPY(val, hv);
		else {
			*str = Z_STRVAL_P(hv);
			*len = Z_STRLEN_P(hv);
		}
	}
	return pos - old;
}

static int decodeRef(const char *buf, int pos, int size, int *num, zval *val, HashTable *ht TSRMLS_DC) {
	int old = pos, ofs, pfx, def;
	ofs = decodeU29(buf, pos, size, &pfx TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) *num = pfx;
	else {
		zval *hv;
		if (!(hv = zend_hash_index_find(ht, pfx))) {
			php_error(E_WARNING, "Invalid object reference %d at position %d", pfx, old);
			return -1;
		}
		*num = -1;
		ZVAL_COPY(val, hv);
	}
	return pos - old;
}

static void storeRef(HashTable *ht, zval *val) {
	zval hv;
	ZVAL_NEW_REF(&hv, val);
	Z_TRY_ADDREF_P(val);
	zend_hash_next_index_insert(ht, &hv);
}

static int decodeDate(const char *buf, int pos, int size, zval *val, HashTable *ht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(buf, pos, size, &pfx, val, ht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (pfx != -1) {
		ofs = decodeDouble(buf, pos, size, val TSRMLS_CC);
		if (ofs < 0) return -1;
		pos += ofs;
		storeRef(ht, val);
	}
	return pos - old;
}

static zval *newIdx(zval *val) {
	zval hv;
	ZVAL_UNDEF(&hv);
	return zend_hash_next_index_insert(HASH_OF(val), &hv);
}

static zval *newKey(zval *val, const char *key, int len) {
	zval hv;
	ZVAL_UNDEF(&hv);
	return zend_symtable_str_update(HASH_OF(val), key, len, &hv);
}

static int decodeArray(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, len;
	ofs = decodeRef(buf, pos, size, &len, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (len != -1) {
		const char *key;
		int klen;
		array_init(val);
		storeRef(oht, val);
		for ( ;; ) { /* Associative portion */
			ofs = decodeString(buf, pos, size, 0, &key, &klen, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			if (!klen) break;
			ofs = decodeValue(buf, pos, size, newKey(val, key, klen), opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		}
		while (len--) { /* Dense portion */
			ofs = decodeValue(buf, pos, size, newIdx(val), opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		}
	}
	return pos - old;
}

static int decodeObject(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(buf, pos, size, &pfx, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
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
			ofs = decodeString(buf, pos, size, 0, &cls, &clen, sht, 0 TSRMLS_CC); /* Class name */
			if (ofs < 0) return -1;
			pos += ofs;
			if (n > 0) {
				if ((pos + n) > size) {
					php_error(E_WARNING, "Invalid number of class members %d at position %d", n, old);
					return -1;
				}
				fld = emalloc(n * sizeof *fld);
				flen = emalloc(n * sizeof *flen);
				for (i = 0; i < n; ++i) { /* Static member names */
					ofs = decodeString(buf, pos, size, 0, &key, &klen, sht, 0 TSRMLS_CC);
					if (ofs < 0) {
						n = -1;
						break;
					}
					pos += ofs;
					if (!klen || !key[0]) {
						php_error(E_WARNING, "Invalid class member name at position %d", pos - ofs);
						n = -1;
						break;
					}
					fld[i] = key;
					flen[i] = klen;
				}
				if (n < 0) {
					efree(fld);
					efree(flen);
					return -1;
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
			php_error(E_WARNING, "Invalid class reference %d at position %d", pfx, old);
			return -1;
		}
		if (!map) array_init(val);
		else {
			if (!tr->cls) object_init(val);
			else {
				int mode = ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_SILENT;
				if (!(opts & AMF3_CLASS_AUTOLOAD)) mode |= ZEND_FETCH_CLASS_NO_AUTOLOAD;
				ce = zend_fetch_class(tr->cls, mode TSRMLS_CC);
				if (!ce) {
					php_error(E_WARNING, "Unknown class '%s' at position %d", ZSTR_VAL(tr->cls), old);
					return -1;
				}
				object_init_ex(val, ce);
			}
		}
		storeRef(oht, val);
		if (tr->fmt & 1) { /* Externalizable */
			ofs = decodeValue(buf, pos, size, newKey(val, "_data", sizeof("_data") - 1), opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		} else {
			int i;
			for (i = 0; i < tr->cnt; ++i) {
				ofs = decodeValue(buf, pos, size, newKey(val, tr->fld[i], tr->flen[i]), opts, sht, oht, tht TSRMLS_CC);
				if (ofs < 0) return -1;
				pos += ofs;
			}
			if (tr->fmt & 2) { /* Dynamic */
				for ( ;; ) {
					ofs = decodeString(buf, pos, size, 0, &key, &klen, sht, 0 TSRMLS_CC);
					if (ofs < 0) return -1;
					pos += ofs;
					if (!klen) break;
					if (map && !key[0]) {
						php_error(E_WARNING, "Invalid class member name at position %d", pos - ofs);
						return -1;
					}
					ofs = decodeValue(buf, pos, size, newKey(val, key, klen), opts, sht, oht, tht TSRMLS_CC);
					if (ofs < 0) return -1;
					pos += ofs;
				}
			}
		}
		if (!map && tr->cls) add_assoc_stringl(val, "_class", ZSTR_VAL(tr->cls), ZSTR_LEN(tr->cls));
		else if (ce && (opts & AMF3_CLASS_CONSTRUCT)) { /* Call the constructor */
			zend_call_method_with_0_params(val, ce, &ce->constructor, NULL, NULL);
			if (EG(exception)) return -1;
		}
	}
	return pos - old;
}

static int decodeVectorItem(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type TSRMLS_DC) {
	switch (type) {
		case AMF3_VECTOR_INT:
			return decodeU32(buf, pos, size, val, 1 TSRMLS_CC);
		case AMF3_VECTOR_UINT:
			return decodeU32(buf, pos, size, val, 0 TSRMLS_CC);
		case AMF3_VECTOR_DOUBLE:
			return decodeDouble(buf, pos, size, val TSRMLS_CC);
		case AMF3_VECTOR_OBJECT:
			return decodeValue(buf, pos, size, val, opts, sht, oht, tht TSRMLS_CC);
		default:
			return -1;
	}
}

static int decodeVector(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type TSRMLS_DC) {
	int old = pos, ofs, len;
	ofs = decodeRef(buf, pos, size, &len, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (len != -1) {
		unsigned char fv;
		ofs = decodeU8(buf, pos, size, &fv TSRMLS_CC); /* 'fixed-vector' marker */
		if (ofs < 0) return -1;
		pos += ofs;
		if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
			const char *ot;
			int otl;
			ofs = decodeString(buf, pos, size, 0, &ot, &otl, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		}
		array_init(val);
		storeRef(oht, val);
		while (len--) {
			ofs = decodeVectorItem(buf, pos, size, newIdx(val), opts, sht, oht, tht, type TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		}
	}
	return pos - old;
}

static int decodeDictionary(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	/* No support for dictionary in PHP */
	php_error(E_WARNING, "Unsupported 'Dictionary' value at position %d", pos);
	return -1;
}

static int decodeValue(const char *buf, int pos, int size, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs;
	unsigned char type;
	ofs = decodeU8(buf, pos, size, &type TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
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
			ofs = decodeInteger(buf, pos, size, val TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_DOUBLE:
			ofs = decodeDouble(buf, pos, size, val TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_STRING:
			ofs = decodeString(buf, pos, size, val, 0, 0, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			ofs = decodeString(buf, pos, size, val, 0, 0, oht, 1 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_DATE:
			ofs = decodeDate(buf, pos, size, val, oht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_ARRAY:
			ofs = decodeArray(buf, pos, size, val, opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_OBJECT:
			ofs = decodeObject(buf, pos, size, val, opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_VECTOR_INT:
		case AMF3_VECTOR_UINT:
		case AMF3_VECTOR_DOUBLE:
		case AMF3_VECTOR_OBJECT:
			ofs = decodeVector(buf, pos, size, val, opts, sht, oht, tht, type TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_DICTIONARY:
			ofs = decodeDictionary(buf, pos, size, val, opts, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		default:
			php_error(E_WARNING, "Invalid value type %d at position %d", type, old);
			return -1;
	}
	return pos - old;
}

static void freeTraits(zval *val) {
	Traits *tr = (Traits *)Z_PTR_P(val);
	if (tr->cls) zend_string_release(tr->cls);
	efree(tr->fld);
	efree(tr->flen);
	efree(tr);
}

PHP_FUNCTION(amf3_decode) {
	const char *buf;
	size_t size;
	zval *count = 0;
	long opts = 0;
	HashTable sht, oht, tht;
	int ofs;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|z/l", &buf, &size, &count, &opts) == FAILURE) return;
	zend_hash_init(&sht, 0, 0, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&oht, 0, 0, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&tht, 0, 0, freeTraits, 0);
	ofs = decodeValue(buf, 0, size, return_value, opts, &sht, &oht, &tht TSRMLS_CC);
	zend_hash_destroy(&sht);
	zend_hash_destroy(&oht);
	zend_hash_destroy(&tht);
	if (count) {
		zval_ptr_dtor(count);
		ZVAL_LONG(count, ofs);
	}
	if (ofs < 0) {
		zval_ptr_dtor(return_value);
		ZVAL_NULL(return_value);
	}
}
