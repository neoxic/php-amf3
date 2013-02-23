/*
** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "ext/standard/info.h"
#include "amf3.h"


static PHP_MINIT_FUNCTION(amf3);
static PHP_MINFO_FUNCTION(amf3);

PHP_FUNCTION(amf3_encode);
PHP_FUNCTION(amf3_decode);


ZEND_BEGIN_ARG_INFO_EX(arginfo_amf3_encode, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_amf3_decode, 0, 0, 1)
	ZEND_ARG_INFO(0, amf3)
	ZEND_ARG_INFO(1, count)
	ZEND_ARG_INFO(0, map)
ZEND_END_ARG_INFO()


static zend_function_entry amf3_functions[] = {
	PHP_FE(amf3_encode, arginfo_amf3_encode)
	PHP_FE(amf3_decode, arginfo_amf3_decode)
	PHP_FE_END
};

zend_module_entry amf3_module_entry = {
	STANDARD_MODULE_HEADER,
	"amf3",
	amf3_functions,
	PHP_MINIT(amf3),
	NULL,
	NULL,
	NULL,
	PHP_MINFO(amf3),
	PHP_AMF3_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_AMF3
ZEND_GET_MODULE(amf3)
#endif


static PHP_MINIT_FUNCTION(amf3) {
	REGISTER_LONG_CONSTANT("AMF3_MAP", AMF3_MAP, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("AMF3_MAP_AUTOLOAD", AMF3_MAP_AUTOLOAD, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("AMF3_MAP_CONSTRUCT", AMF3_MAP_CONSTRUCT, CONST_CS | CONST_PERSISTENT);
	return SUCCESS;
}

static PHP_MINFO_FUNCTION(amf3) {
	php_info_print_table_start();
	php_info_print_table_row(2, "AMF3 support", "enabled");
	php_info_print_table_row(2, "Version", PHP_AMF3_VERSION);
	php_info_print_table_end();
}
