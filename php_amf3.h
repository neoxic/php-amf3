/*
** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifndef php_amf3_h
#define php_amf3_h


#define PHP_AMF3_VERSION "1.2.0"

extern zend_module_entry amf3_module_entry;
#define phpext_amf3_ptr &amf3_module_entry

#ifdef PHP_WIN32
#define PHP_AMF3_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define PHP_AMF3_API __attribute__ ((visibility("default")))
#else
#define PHP_AMF3_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif


#endif
