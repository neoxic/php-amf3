/*
** Copyright (C) 2010, 2013-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifndef PHP_AMF3_H
#define PHP_AMF3_H


#define PHP_AMF3_VERSION "1.2.2"

extern zend_module_entry amf3_module_entry;
#define phpext_amf3_ptr &amf3_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(amf3);
PHP_MINFO_FUNCTION(amf3);

PHP_FUNCTION(amf3_encode);
PHP_FUNCTION(amf3_decode);


#endif
