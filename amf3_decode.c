/*
** Copyright (C) 2010, 2013-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
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
	char *cls;
	int clen;
	char **fld;
	int *flen;
} Traits;


static void storeRef(HashTable *ht, zval *val) {
	Z_ADDREF_P(val);
	zend_hash_index_update(ht, zend_hash_num_elements(ht), &val, sizeof val, NULL);
}

static void traitsPtrDtor(void *p) {
	Traits *tr = *((Traits **)p);
	int n = tr->cnt;
	if (n > 0) {
		while (n--) efree(tr->fld[n]);
		efree(tr->fld);
		efree(tr->flen);
	}
	efree(tr->cls);
	efree(tr);
}

static int decodeValue(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC);

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

static int decodeInteger(const char *buf, int pos, int size, zval **val TSRMLS_DC) {
	int n, ofs = decodeU29(buf, pos, size, &n TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (n & 0x10000000) n -= 0x20000000;
	ZVAL_RESET(*val);
	ZVAL_LONG(*val, n);
	return ofs;
}

static int decodeU32(const char *buf, int pos, int size, zval **val, int sign TSRMLS_DC) {
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
	ZVAL_RESET(*val);
	ZVAL_LONG(*val, n);
	return 4;
}

static int decodeDouble(const char *buf, int pos, int size, zval **val TSRMLS_DC) {
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
	ZVAL_RESET(*val);
	ZVAL_DOUBLE(*val, u.d);
	return 8;
}

static int decodeString(const char *buf, int pos, int size, const char **str, int *len, zval **val, HashTable *ht, int blob TSRMLS_DC) {
	int old = pos, ofs, pfx, def;
	ofs = decodeU29(buf, pos, size, &pfx TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) {
		if ((pos + pfx) > size) {
			php_error(E_WARNING, "Invalid length %d at position %d", pfx, old);
			return -1;
		}
		buf += pos;
		pos += pfx;
		if (str && len) {
			*str = buf;
			*len = pfx;
		} else if (val) {
			ZVAL_RESET(*val);
			ZVAL_STRINGL(*val, buf, pfx, 1);
		}
		if (blob || pfx) { /* Empty string is never sent by reference */
			zval *hv;
			if (val) {
				hv = *val;
				Z_ADDREF_P(hv);
			} else {
				ALLOC_INIT_ZVAL(hv);
				ZVAL_STRINGL(hv, buf, pfx, 1);
			}
			zend_hash_index_update(ht, zend_hash_num_elements(ht), &hv, sizeof hv, NULL);
		}
	} else {
		zval **hv;
		if (zend_hash_index_find(ht, pfx, (void **)&hv) == FAILURE) {
			php_error(E_WARNING, "Invalid reference %d at position %d", pfx, old);
			return -1;
		}
		if (str && len) {
			*str = Z_STRVAL_PP(hv);
			*len = Z_STRLEN_PP(hv);
		} else if (val) {
			*val = *hv;
			Z_ADDREF_PP(hv);
		}
	}
	return pos - old;
}

static int decodeRef(const char *buf, int pos, int size, int *num, zval **val, HashTable *ht TSRMLS_DC) {
	int old = pos, ofs, pfx, def;
	ofs = decodeU29(buf, pos, size, &pfx TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) *num = pfx;
	else {
		zval **hv;
		if (zend_hash_index_find(ht, pfx, (void **)&hv) == FAILURE) {
			php_error(E_WARNING, "Invalid reference %d at position %d", pfx, old);
			return -1;
		}
		*num = -1;
		*val = *hv;
		Z_ADDREF_PP(hv);
	}
	return pos - old;
}

static int decodeDate(const char *buf, int pos, int size, zval **val, HashTable *ht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(buf, pos, size, &pfx, val, ht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (pfx != -1) {
		ofs = decodeDouble(buf, pos, size, val TSRMLS_CC);
		if (ofs < 0) return -1;
		pos += ofs;
		storeRef(ht, *val);
	}
	return pos - old;
}

static int decodeArray(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, len;
	ofs = decodeRef(buf, pos, size, &len, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (len != -1) {
		zval *hv;
		const char *key;
		char kbuf[64];
		int klen;
		ZVAL_RESET(*val);
		array_init(*val);
		storeRef(oht, *val);
		for ( ;; ) { /* Associative portion */
			ofs = decodeString(buf, pos, size, &key, &klen, 0, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			if (!klen) break;
			hv = 0;
			ofs = decodeValue(buf, pos, size, &hv, opts, sht, oht, tht TSRMLS_CC);
			if (hv) { /* Need a trailing \0 in the key name for a proper call to 'add_assoc_zval_ex' */
				if (klen < sizeof kbuf) {
					memcpy(kbuf, key, klen);
					kbuf[klen] = 0;
					add_assoc_zval_ex(*val, kbuf, klen + 1, hv);
				} else {
					char *tbuf = emalloc(klen + 1);
					memcpy(tbuf, key, klen);
					tbuf[klen] = 0;
					add_assoc_zval_ex(*val, tbuf, klen + 1, hv);
					efree(tbuf);
				}
			}
			if (ofs < 0) return -1;
			pos += ofs;
		}
		while (len--) { /* Dense portion */
			hv = 0;
			ofs = decodeValue(buf, pos, size, &hv, opts, sht, oht, tht TSRMLS_CC);
			if (hv) add_next_index_zval(*val, hv);
			if (ofs < 0) return -1;
			pos += ofs;
		}
	}
	return pos - old;
}

static int decodeObject(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(buf, pos, size, &pfx, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (pfx != -1) {
		int map = opts & AMF3_CLASS_MAP;
		zend_class_entry *ce = 0;
		Traits *tr;
		zval *hv;
		const char *key;
		char kbuf[64];
		int klen;
		int def = pfx & 1;
		pfx >>= 1;
		if (def) { /* New class definition */
			int i, n = pfx >> 2;
			const char *cls;
			int clen;
			char **fld;
			int *flen;
			ofs = decodeString(buf, pos, size, &cls, &clen, 0, sht, 0 TSRMLS_CC); /* Class name */
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
					ofs = decodeString(buf, pos, size, &key, &klen, 0, sht, 0 TSRMLS_CC);
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
					fld[i] = estrndup(key, klen); /* Trailing \0 is needed later in a key name */
					flen[i] = klen + 1;
				}
				if (n < 0) {
					while (i--) efree(fld[i]);
					efree(fld);
					efree(flen);
					return -1;
				}
			}
			tr = emalloc(sizeof(Traits));
			tr->fmt = pfx & 3;
			tr->cnt = n;
			tr->cls = estrndup(cls, clen);
			tr->clen = clen;
			tr->fld = fld;
			tr->flen = flen;
			zend_hash_index_update(tht, zend_hash_num_elements(tht), &tr, sizeof tr, NULL);
		} else { /* Existing class definition */
			Traits **trp;
			if (zend_hash_index_find(tht, pfx, (void **)&trp) == FAILURE) {
				php_error(E_WARNING, "Invalid class reference %d at position %d", pfx, old);
				return -1;
			}
			tr = *trp;
		}
		ZVAL_RESET(*val);
		if (!map) array_init(*val);
		else {
			if (!tr->clen) object_init(*val);
			else {
				int mode = ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_SILENT;
				if (!(opts & AMF3_CLASS_AUTOLOAD)) mode |= ZEND_FETCH_CLASS_NO_AUTOLOAD;
				ce = zend_fetch_class(tr->cls, tr->clen, mode TSRMLS_CC);
				if (!ce) {
					php_error(E_WARNING, "Class '%s' not found at position %d", tr->cls, old);
					return -1;
				}
				object_init_ex(*val, ce);
			}
		}
		storeRef(oht, *val);
		if (tr->fmt & 1) { /* Externalizable */
			hv = 0;
			ofs = decodeValue(buf, pos, size, &hv, opts, sht, oht, tht TSRMLS_CC);
			if (hv) {
				if (!map) add_assoc_zval(*val, "_data", hv);
				else {
					add_property_zval(*val, "_data", hv);
					Z_DELREF_P(hv);
				}
			}
			if (ofs < 0) return -1;
			pos += ofs;
		} else {
			int i;
			for (i = 0; i < tr->cnt; ++i) {
				hv = 0;
				ofs = decodeValue(buf, pos, size, &hv, opts, sht, oht, tht TSRMLS_CC);
				if (hv) {
					if (!map) add_assoc_zval_ex(*val, tr->fld[i], tr->flen[i], hv);
					else {
						add_property_zval_ex(*val, tr->fld[i], tr->flen[i], hv TSRMLS_CC);
						Z_DELREF_P(hv);
					}
				}
				if (ofs < 0) return -1;
				pos += ofs;
			}
			if (tr->fmt & 2) { /* Dynamic */
				for ( ;; ) {
					ofs = decodeString(buf, pos, size, &key, &klen, 0, sht, 0 TSRMLS_CC);
					if (ofs < 0) return -1;
					pos += ofs;
					if (!klen) break;
					if (map && !key[0]) {
						php_error(E_WARNING, "Invalid class member name at position %d", pos - ofs);
						return -1;
					}
					hv = 0;
					ofs = decodeValue(buf, pos, size, &hv, opts, sht, oht, tht TSRMLS_CC);
					if (hv) { /* Need a trailing \0 in the key name for a proper call to 'add_property_zval_ex' */
						if (klen < sizeof kbuf) {
							memcpy(kbuf, key, klen);
							kbuf[klen] = 0;
							if (!map) add_assoc_zval_ex(*val, kbuf, klen + 1, hv);
							else {
								add_property_zval_ex(*val, kbuf, klen + 1, hv TSRMLS_CC);
								Z_DELREF_P(hv);
							}
						} else {
							char *tbuf = emalloc(klen + 1);
							memcpy(tbuf, key, klen);
							tbuf[klen] = 0;
							if (!map) add_assoc_zval_ex(*val, tbuf, klen + 1, hv);
							else {
								add_property_zval_ex(*val, tbuf, klen + 1, hv TSRMLS_CC);
								Z_DELREF_P(hv);
							}
							efree(tbuf);
						}
					}
					if (ofs < 0) return -1;
					pos += ofs;
				}
			}
		}
		if (!map && tr->clen) add_assoc_stringl(*val, "_class", tr->cls, tr->clen, 1);
		else if (ce && (opts & AMF3_CLASS_CONSTRUCT)) { /* Call the constructor */
			zend_call_method_with_0_params(val, ce, &ce->constructor, NULL, NULL);
			if (EG(exception)) return -1;
		}
	}
	return pos - old;
}

static int decodeVectorItem(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type TSRMLS_DC) {
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

static int decodeVector(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht, int type TSRMLS_DC) {
	int old = pos, ofs, len;
	ofs = decodeRef(buf, pos, size, &len, val, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (len != -1) {
		zval *hv;
		unsigned char fv;
		ofs = decodeU8(buf, pos, size, &fv TSRMLS_CC); /* 'fixed-vector' marker */
		if (ofs < 0) return -1;
		pos += ofs;
		if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
			ofs = decodeString(buf, pos, size, 0, 0, 0, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
		}
		ZVAL_RESET(*val);
		array_init(*val);
		storeRef(oht, *val);
		while (len--) {
			hv = 0;
			ofs = decodeVectorItem(buf, pos, size, &hv, opts, sht, oht, tht, type TSRMLS_CC);
			if (hv) add_next_index_zval(*val, hv);
			if (ofs < 0) return -1;
			pos += ofs;
		}
	}
	return pos - old;
}

static int decodeDictionary(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	/* No support for dictionary in PHP */
	php_error(E_WARNING, "Unsupported 'Dictionary' value at position %d", pos);
	return -1;
}

static int decodeValue(const char *buf, int pos, int size, zval **val, int opts, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs;
	unsigned char type;
	ofs = decodeU8(buf, pos, size, &type TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	switch (type) {
		case AMF3_UNDEFINED:
		case AMF3_NULL:
			ZVAL_RESET(*val);
			ZVAL_NULL(*val);
			break;
		case AMF3_FALSE:
			ZVAL_RESET(*val);
			ZVAL_FALSE(*val);
			break;
		case AMF3_TRUE:
			ZVAL_RESET(*val);
			ZVAL_TRUE(*val);
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
			ofs = decodeString(buf, pos, size, 0, 0, val, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			ofs = decodeString(buf, pos, size, 0, 0, val, oht, 1 TSRMLS_CC);
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

PHP_FUNCTION(amf3_decode) {
	const char *buf;
	int size, ofs;
	zval *count = 0;
	long opts = 0;
	HashTable sht, oht, tht;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|zl", &buf, &size, &count, &opts) == FAILURE) return;
	zend_hash_init(&sht, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&oht, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&tht, 0, NULL, traitsPtrDtor, 0);
	ofs = decodeValue(buf, 0, size, &return_value, opts, &sht, &oht, &tht TSRMLS_CC);
	zend_hash_destroy(&sht);
	zend_hash_destroy(&oht);
	zend_hash_destroy(&tht);
	if (count) {
		zval_dtor(count);
		ZVAL_LONG(count, ofs);
	}
	if (ofs < 0) {
		zval_dtor(return_value);
		ZVAL_NULL(return_value);
	}
}
