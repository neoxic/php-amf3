/*
** Copyright (C) 2010, 2013-2016 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifndef AMF3_H
#define AMF3_H


#define AMF3_UNDEFINED     0x00
#define AMF3_NULL          0x01
#define AMF3_FALSE         0x02
#define AMF3_TRUE          0x03
#define AMF3_INTEGER       0x04
#define AMF3_DOUBLE        0x05
#define AMF3_STRING        0x06
#define AMF3_XMLDOC        0x07
#define AMF3_DATE          0x08
#define AMF3_ARRAY         0x09
#define AMF3_OBJECT        0x0a
#define AMF3_XML           0x0b
#define AMF3_BYTEARRAY     0x0c
#define AMF3_VECTOR_INT    0x0d
#define AMF3_VECTOR_UINT   0x0e
#define AMF3_VECTOR_DOUBLE 0x0f
#define AMF3_VECTOR_OBJECT 0x10
#define AMF3_DICTIONARY    0x11

#define AMF3_MAX_INT  268435455 /*  (2^28)-1 */
#define AMF3_MIN_INT -268435456 /* -(2^28)   */

// Encoding options
#define AMF3_FORCE_OBJECT 0x01

// Decoding options
#define AMF3_CLASS_MAP       0x01
#define AMF3_CLASS_AUTOLOAD  0x02
#define AMF3_CLASS_CONSTRUCT 0x04


#endif
