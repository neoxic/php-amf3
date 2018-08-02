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

#define AMF3_INT_MIN -268435456
#define AMF3_INT_MAX 268435455

// Encoding options
#define AMF3_FORCE_OBJECT 0x01

// Decoding options
#define AMF3_CLASS_MAP       0x01
#define AMF3_CLASS_AUTOLOAD  0x02
#define AMF3_CLASS_CONSTRUCT 0x04

#endif
