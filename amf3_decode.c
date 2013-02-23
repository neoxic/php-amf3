/*
** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "Zend/zend_interfaces.h"
#include "amf3.h"


typedef struct {
	int fmt, cnt;
	const char *cls;
	int clsLen;
	char **fld;
	int *fldLen;
} Traits;


static int decodeValue(zval **val, const char* buf, int pos, int size, int map, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC);

static int decodeU29(int *val, const char *buf, int pos, int size TSRMLS_DC) {
	int ofs = 0, res = 0, tmp;
	buf += pos;
	do {
		if ((pos + ofs) >= size) {
			php_error(E_WARNING, "Insufficient integer data at position %d", pos);
			return -1;
		}
		tmp = buf[ofs];
		if (ofs == 3) {
			res <<= 8;
			res |= tmp & 0xff;
		} else {
			res <<= 7;
			res |= tmp & 0x7f;
		}
	} while ((++ofs < 4) && (tmp & 0x80));
	*val = res;
	return ofs;
}

static int decodeDouble(double *val, const char *buf, int pos, int size TSRMLS_DC) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	if ((pos + 8) > size) {
		php_error(E_WARNING, "Insufficient number data at position %d", pos);
		return -1;
	}
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	*val = u.d;
	return 8;
}

static int decodeStr(const char **str, int *len, zval **val, const char* buf, int pos, int size, HashTable *ht, int loose TSRMLS_DC) {
	int old = pos, ofs, pfx, def;
	ofs = decodeU29(&pfx, buf, pos, size TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) {
		if ((pos + pfx) > size) {
			php_error(E_WARNING, "Insufficient data of length %d at position %d", pfx, pos);
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
		if (loose || pfx) { /* empty string is never sent by reference */
			zval *hv;
			if (val) {
				hv = *val;
				Z_ADDREF_P(hv);
			} else {
				ALLOC_INIT_ZVAL(hv);
				ZVAL_STRINGL(hv, buf, pfx, 1);
			}
			zend_hash_index_update(ht, zend_hash_num_elements(ht), &hv, sizeof(hv), NULL);
		}
	} else {
		zval **hv;
		if (zend_hash_index_find(ht, pfx, (void **)&hv) == FAILURE) {
			php_error(E_WARNING, "Missing string reference #%d at position %d", pfx, pos);
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

static int decodeRef(int *len, zval **val, const char* buf, int pos, int size, HashTable *ht TSRMLS_DC) {
	int ofs, pfx, def;
	ofs = decodeU29(&pfx, buf, pos, size TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	def = pfx & 1;
	pfx >>= 1;
	if (def) *len = pfx;
	else {
		zval **hv;
		if (zend_hash_index_find(ht, pfx, (void **)&hv) == FAILURE) {
			php_error(E_WARNING, "Missing object reference #%d at position %d", pfx, pos);
			return -1;
		}
		*len = -1;
		*val = *hv;
		Z_ADDREF_PP(hv);
	}
	return ofs;
}

static void storeRef(zval *val, HashTable *ht) {
	zend_hash_index_update(ht, zend_hash_num_elements(ht), &val, sizeof(val), NULL);
	Z_ADDREF_P(val);
}

static int decodeDate(zval **val, const char* buf, int pos, int size, HashTable *ht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(&pfx, val, buf, pos, size, ht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (pfx >= 0) {
		double d;
		ofs = decodeDouble(&d, buf, pos, size TSRMLS_CC);
		if (ofs < 0) return -1;
		pos += ofs;
		ZVAL_RESET(*val);
		ZVAL_DOUBLE(*val, d);
		storeRef(*val, ht);
	}
	return pos - old;
}

static int decodeArray(zval **val, const char* buf, int pos, int size, int map, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, len;
	ofs = decodeRef(&len, val, buf, pos, size, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (len >= 0) {
		zval *hv;
		const char *key;
		char keyBuf[64];
		int keyLen;
		ZVAL_RESET(*val);
		array_init(*val);
		storeRef(*val, oht);
		for ( ;; ) { /* associative portion */
			ofs = decodeStr(&key, &keyLen, 0, buf, pos, size, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			if (!keyLen) break;
			hv = 0;
			ofs = decodeValue(&hv, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
			if (hv) { /* need a trailing \0 in the key string for a proper call to 'add_assoc_zval_ex' */
				if (keyLen < sizeof(keyBuf)) {
					memcpy(keyBuf, key, keyLen);
					keyBuf[keyLen] = 0;
					add_assoc_zval_ex(*val, keyBuf, keyLen + 1, hv);
				} else {
					char *tmpBuf = emalloc(keyLen + 1);
					memcpy(tmpBuf, key, keyLen);
					tmpBuf[keyLen] = 0;
					add_assoc_zval_ex(*val, tmpBuf, keyLen + 1, hv);
					efree(tmpBuf);
				}
			}
			if (ofs < 0) return -1;
			pos += ofs;
		}
		while (len--) { /* dense portion */
			hv = 0;
			ofs = decodeValue(&hv, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
			if (hv) add_next_index_zval(*val, hv);
			if (ofs < 0) return -1;
			pos += ofs;
		}
	}
	return pos - old;
}

static int decodeObject(zval **val, const char* buf, int pos, int size, int map, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs, pfx;
	ofs = decodeRef(&pfx, val, buf, pos, size, oht TSRMLS_CC);
	if (ofs < 0) return -1;
	pos += ofs;
	if (pfx >= 0) {
		zend_class_entry *ce = 0;
		Traits *tr;
		zval *hv;
		const char *key;
		char keyBuf[64];
		int keyLen;
		int def = pfx & 1;
		pfx >>= 1;
		if (def) { /* new class definition */
			int i, n = pfx >> 2;
			const char *cls;
			int clsLen;
			char **fld = 0;
			int *fldLen = 0;
			ofs = decodeStr(&cls, &clsLen, 0, buf, pos, size, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			if (n > 0) {
				fld = emalloc(sizeof(*fld) * n);
				fldLen = emalloc(sizeof(*fldLen) * n);
				for (i = 0; i < n; ++i) { /* static member names */
					ofs = decodeStr(&key, &keyLen, 0, buf, pos, size, sht, 0 TSRMLS_CC);
					if (ofs < 0) {
						while (i--) efree(fld[i]);
						efree(fld);
						efree(fldLen);
						return -1;
					}
					pos += ofs;
					fld[i] = estrndup(key, keyLen); /* a trailing \0 is needed later for a key string */
					fldLen[i] = keyLen + 1;
				}
			}
			char tmp[20];
			memset(tmp, 0, 20);
			memcpy(tmp, cls, clsLen);
			tr = emalloc(sizeof(Traits));
			tr->fmt = pfx & 0x03;
			tr->cnt = n;
			tr->cls = cls;
			tr->clsLen = clsLen;
			tr->fld = fld;
			tr->fldLen = fldLen;
			zend_hash_index_update(tht, zend_hash_num_elements(tht), &tr, sizeof(tr), NULL);
		} else { /* existing class definition */
			Traits **trp;
			if (zend_hash_index_find(tht, pfx, (void **)&trp) == FAILURE) {
				php_error(E_WARNING, "Missing class definition #%d at position %d", pfx, pos);
				return -1;
			}
			tr = *trp;
		}
		ZVAL_RESET(*val);
		if (!map) array_init(*val);
		else {
			if (!tr->clsLen) object_init(*val);
			else {
				int mode = ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_SILENT;
				if (!(map & AMF3_MAP_AUTOLOAD)) mode |= ZEND_FETCH_CLASS_NO_AUTOLOAD;
				ce = zend_fetch_class(tr->cls, tr->clsLen, mode TSRMLS_CC);
				if (!ce) {
					php_error(E_WARNING, "Class '%s' fetching failed at position %d", tr->cls, pos);
					return -1;
				}
				object_init_ex(*val, ce);
			}
		}
		storeRef(*val, oht);
		if (tr->fmt & 1) { /* externalizable */
			hv = 0;
			ofs = decodeValue(&hv, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
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
				ofs = decodeValue(&hv, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
				if (hv) {
					if (!map) add_assoc_zval_ex(*val, tr->fld[i], tr->fldLen[i], hv);
					else {
						add_property_zval_ex(*val, tr->fld[i], tr->fldLen[i], hv TSRMLS_CC);
						Z_DELREF_P(hv);
					}
				}
				if (ofs < 0) return -1;
				pos += ofs;
			}
			if (tr->fmt & 2) { /* dynamic */
				for ( ;; ) {
					ofs = decodeStr(&key, &keyLen, 0, buf, pos, size, sht, 0 TSRMLS_CC);
					if (ofs < 0) return -1;
					pos += ofs;
					if (!keyLen) break;
					hv = 0;
					ofs = decodeValue(&hv, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
					if (hv) { /* need a trailing \0 in the key string for a proper call to 'add_property_zval_ex' */
						if (keyLen < sizeof(keyBuf)) {
							memcpy(keyBuf, key, keyLen);
							keyBuf[keyLen] = 0;
							if (!map) add_assoc_zval_ex(*val, keyBuf, keyLen + 1, hv);
							else {
								add_property_zval_ex(*val, keyBuf, keyLen + 1, hv TSRMLS_CC);
								Z_DELREF_P(hv);
							}
						} else {
							char *tmpBuf = emalloc(keyLen + 1);
							memcpy(tmpBuf, key, keyLen);
							tmpBuf[keyLen] = 0;
							if (!map) add_assoc_zval_ex(*val, tmpBuf, keyLen + 1, hv);
							else {
								add_property_zval_ex(*val, tmpBuf, keyLen + 1, hv TSRMLS_CC);
								Z_DELREF_P(hv);
							}
							efree(tmpBuf);
						}
					}
					if (ofs < 0) return -1;
					pos += ofs;
				}
			}
		}
		if (!map && tr->clsLen) add_assoc_stringl(*val, "_class", (char *)tr->cls, tr->clsLen, 1);
		else if (ce && (map & AMF3_MAP_CONSTRUCT)) { /* call the constructor */
			zend_call_method_with_0_params(val, ce, &ce->constructor, NULL, NULL);
			if (EG(exception)) return -1;
		}
	}
	return pos - old;
}

static int decodeValue(zval **val, const char* buf, int pos, int size, int map, HashTable *sht, HashTable *oht, HashTable *tht TSRMLS_DC) {
	int old = pos, ofs;
	if (pos >= size) {
		php_error(E_WARNING, "Insufficient type data at position %d", pos);
		return -1;
	}
	switch (buf[pos++]) {
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
		case AMF3_INTEGER: {
			int i;
			ofs = decodeU29(&i, buf, pos, size TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			if (i & 0x10000000) i -= 0x20000000;
			ZVAL_RESET(*val);
			ZVAL_LONG(*val, i);
			break;
		}
		case AMF3_DOUBLE: {
			double d;
			ofs = decodeDouble(&d, buf, pos, size TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			ZVAL_RESET(*val);
			ZVAL_DOUBLE(*val, d);
			break;
		}
		case AMF3_STRING:
			ofs = decodeStr(0, 0, val, buf, pos, size, sht, 0 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			ofs = decodeStr(0, 0, val, buf, pos, size, oht, 1 TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_DATE:
			ofs = decodeDate(val, buf, pos, size, oht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_ARRAY:
			ofs = decodeArray(val, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		case AMF3_OBJECT:
			ofs = decodeObject(val, buf, pos, size, map, sht, oht, tht TSRMLS_CC);
			if (ofs < 0) return -1;
			pos += ofs;
			break;
		default:
			php_error(E_WARNING, "Invalid type value %d at position %d", buf[pos - 1], pos - 1);
			return -1;
	}
	return pos - old;
}

static void traitsPtrDtor(void *p) {
	Traits *tr = *((Traits **)p);
	int n = tr->cnt;
	if (n > 0) {
		while (n--) efree(tr->fld[n]);
		efree(tr->fld);
		efree(tr->fldLen);
	}
	efree(tr);
}

PHP_FUNCTION(amf3_decode) {
	const char *buf;
	int size, ofs;
	zval *count = 0;
	long map = 0;
	HashTable sht, oht, tht;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|zl", &buf, &size, &count, &map) == FAILURE) return;
	zend_hash_init(&sht, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&oht, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&tht, 0, NULL, traitsPtrDtor, 0);
	ofs = decodeValue(&return_value, buf, 0, size, map, &sht, &oht, &tht TSRMLS_CC);
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
