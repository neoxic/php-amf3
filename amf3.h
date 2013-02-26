/*
** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifndef amf3_h
#define amf3_h


#define AMF3_UNDEFINED   0x00
#define AMF3_NULL        0x01
#define AMF3_FALSE       0x02
#define AMF3_TRUE        0x03
#define AMF3_INTEGER     0x04
#define AMF3_DOUBLE      0x05
#define AMF3_STRING      0x06
#define AMF3_XMLDOC      0x07
#define AMF3_DATE        0x08
#define AMF3_ARRAY       0x09
#define AMF3_OBJECT      0x0a
#define AMF3_XML         0x0b
#define AMF3_BYTEARRAY   0x0c

#define AMF3_MAX_INT     268435455 /*  (2^28)-1 */
#define AMF3_MIN_INT    -268435456 /* -(2^28)   */

// Mapping mode bits
#define AMF3_MAP         0x01
#define AMF3_AUTOLOAD    0x02
#define AMF3_CONSTRUCT   0x04

#define ZVAL_RESET(A)         \
	if (!(A)) {               \
		ALLOC_INIT_ZVAL((A)); \
	} else {                  \
		zval_dtor((A));       \
		ZVAL_NULL((A));       \
	}

/* PHP 5.2, old GC */
#ifndef Z_ADDREF_P
#define Z_ADDREF_P(A) ZVAL_ADDREF(A)
#endif
#ifndef Z_ADDREF_PP
#define Z_ADDREF_PP(A) ZVAL_ADDREF(*(A))
#endif
#ifndef Z_DELREF_P
#define Z_DELREF_P(A) ZVAL_DELREF(A)
#endif
#ifndef Z_DELREF_PP
#define Z_DELREF_PP(A) ZVAL_DELREF(*(A))
#endif


#endif
