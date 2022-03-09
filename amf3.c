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
#include "ext/standard/info.h"
#include "amf3.h"

ZEND_BEGIN_ARG_INFO_EX(arginfo_amf3_encode, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_amf3_decode, 0, 0, 1)
	ZEND_ARG_INFO(0, amf3)
	ZEND_ARG_INFO(1, count)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_AMF3Serializable___toAMF3, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry amf3_functions[] = {
	PHP_FE(amf3_encode, arginfo_amf3_encode)
	PHP_FE(amf3_decode, arginfo_amf3_decode)
	PHP_FE_END
};

static const zend_function_entry class_AMF3Serializable_methods[] = {
	PHP_ABSTRACT_ME(AMF3Serializable, __toAMF3, arginfo_AMF3Serializable___toAMF3)
	PHP_FE_END
};

zend_class_entry *amf3_serializable_ce;

zend_module_entry amf3_module_entry = {
	STANDARD_MODULE_HEADER,
	"amf3",
	amf3_functions,
	PHP_MINIT(amf3),
	0,
	0,
	0,
	PHP_MINFO(amf3),
	PHP_AMF3_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_AMF3
ZEND_GET_MODULE(amf3)
#endif

PHP_MINIT_FUNCTION(amf3) {
	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "AMF3Serializable", class_AMF3Serializable_methods);
	amf3_serializable_ce = zend_register_internal_interface(&ce);
	REGISTER_LONG_CONSTANT("AMF3_FORCE_OBJECT", AMF3_FORCE_OBJECT, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("AMF3_CLASS_MAP", AMF3_CLASS_MAP, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("AMF3_CLASS_AUTOLOAD", AMF3_CLASS_AUTOLOAD, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("AMF3_CLASS_CONSTRUCT", AMF3_CLASS_CONSTRUCT, CONST_CS | CONST_PERSISTENT);
	return SUCCESS;
}

PHP_MINFO_FUNCTION(amf3) {
	php_info_print_table_start();
	php_info_print_table_row(2, "AMF3 support", "enabled");
	php_info_print_table_row(2, "Version", PHP_AMF3_VERSION);
	php_info_print_table_end();
}
