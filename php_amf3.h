/*
 * ========================================================================
 * PHP AMF3 encoding/decoding extension
 * Work started by Arseny Vakhrushev on 11 Jan 2010
 * Copyright (C) 2010 IT Territory, LLC. http://it-territory.ru/
 * ========================================================================
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Please read the LICENSE file for license details
 */

#ifndef PHP_AMF3_H
#define PHP_AMF3_H 1

#define PHP_AMF3_WORLD_VERSION "1.0"
#define PHP_AMF3_WORLD_EXTNAME "amf3"

PHP_FUNCTION(amf3_encode);
PHP_FUNCTION(amf3_decode);

extern zend_module_entry hello_module_entry;
#define phpext_hello_ptr &hello_module_entry

#endif
