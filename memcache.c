/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Antony Dovgal <tony2001@phpclub.net>                        |
  |          Mikael Johansson <mikael AT synd DOT info>                  |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include <stdio.h>
#include <fcntl.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#include <time.h>
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"
#include "ext/standard/php_smart_str.h"
#include "php_memcache.h"

#ifndef ZEND_ENGINE_2
#define OnUpdateLong OnUpdateInt
#endif

/* True global resources - no need for thread safety here */
static int le_memcache_pool, le_memcache_server;
static zend_class_entry *memcache_pool_ce;
static zend_class_entry *memcache_ce;

ZEND_EXTERN_MODULE_GLOBALS(memcache)

/* {{{ memcache_functions[]
 */
zend_function_entry memcache_functions[] = {
	PHP_FE(memcache_connect,				NULL)
	PHP_FE(memcache_pconnect,				NULL)
	PHP_FE(memcache_add_server,				NULL)
	PHP_FE(memcache_set_server_params,		NULL)
	PHP_FE(memcache_get_server_status,		NULL)
	PHP_FE(memcache_get_version,			NULL)
	PHP_FE(memcache_add,					NULL)
	PHP_FE(memcache_set,					NULL)
	PHP_FE(memcache_replace,				NULL)
	PHP_FE(memcache_get,					NULL)
	PHP_FE(memcache_delete,					NULL)
	PHP_FE(memcache_debug,					NULL)
	PHP_FE(memcache_get_stats,				NULL)
	PHP_FE(memcache_get_extended_stats,		NULL)
	PHP_FE(memcache_set_compress_threshold,	NULL)
	PHP_FE(memcache_increment,				NULL)
	PHP_FE(memcache_decrement,				NULL)
	PHP_FE(memcache_close,					NULL)
	PHP_FE(memcache_flush,					NULL)
	{NULL, NULL, NULL}
};

static zend_function_entry php_memcache_pool_class_functions[] = {
	PHP_NAMED_FE(connect,				zif_memcache_pool_connect,			NULL)
	PHP_NAMED_FE(addserver,				zif_memcache_pool_addserver,		NULL)
	PHP_FALIAS(setserverparams,			memcache_set_server_params,			NULL)
	PHP_FALIAS(getserverstatus,			memcache_get_server_status,			NULL)
	PHP_FALIAS(getversion,				memcache_get_version,				NULL)
	PHP_FALIAS(add,						memcache_add,						NULL)
	PHP_FALIAS(set,						memcache_set,						NULL)
	PHP_FALIAS(replace,					memcache_replace,					NULL)
	PHP_FALIAS(get,						memcache_get,						NULL)
	PHP_FALIAS(delete,					memcache_delete,					NULL)
	PHP_FALIAS(getstats,				memcache_get_stats,					NULL)
	PHP_FALIAS(getextendedstats,		memcache_get_extended_stats,		NULL)
	PHP_FALIAS(setcompressthreshold,	memcache_set_compress_threshold,	NULL)
	PHP_FALIAS(increment,				memcache_increment,					NULL)
	PHP_FALIAS(decrement,				memcache_decrement,					NULL)
	PHP_FALIAS(close,					memcache_close,						NULL)
	PHP_FALIAS(flush,					memcache_flush,						NULL)
	{NULL, NULL, NULL}
};

static zend_function_entry php_memcache_class_functions[] = {
	PHP_FALIAS(connect,					memcache_connect,					NULL)
	PHP_FALIAS(pconnect,				memcache_pconnect,					NULL)
	PHP_FALIAS(addserver,				memcache_add_server,				NULL)
	{NULL, NULL, NULL}
};

/* }}} */

/* {{{ memcache_module_entry
 */
zend_module_entry memcache_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"memcache",
	memcache_functions,
	PHP_MINIT(memcache),
	PHP_MSHUTDOWN(memcache),
	NULL,
	NULL,
	PHP_MINFO(memcache),
#if ZEND_MODULE_API_NO >= 20010901
	NO_VERSION_YET, 			/* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MEMCACHE
ZEND_GET_MODULE(memcache)
#endif

static PHP_INI_MH(OnUpdateChunkSize) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.chunk_size must be a positive integer ('%s' given)", new_value);
		return FAILURE;
	}

	return OnUpdateLong(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
}
/* }}} */

static PHP_INI_MH(OnUpdateFailoverAttempts) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.max_failover_attempts must be a positive integer ('%s' given)", new_value);
		return FAILURE;
	}

	return OnUpdateLong(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
}
/* }}} */

static PHP_INI_MH(OnUpdateHashStrategy) /* {{{ */
{
	if (!strcasecmp(new_value, "standard")) {
		MEMCACHE_G(hash_strategy) = MMC_STANDARD_HASH;
	}
	else if (!strcasecmp(new_value, "consistent")) {
		MEMCACHE_G(hash_strategy) = MMC_CONSISTENT_HASH;
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.hash_strategy must be in set {standard, consistent} ('%s' given)", new_value);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateHashFunction) /* {{{ */
{
	if (!strcasecmp(new_value, "crc32")) {
		MEMCACHE_G(hash_function) = MMC_HASH_CRC32;
	}
	else if (!strcasecmp(new_value, "fnv")) {
		MEMCACHE_G(hash_function) = MMC_HASH_FNV1A;
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.hash_function must be in set {crc32, fnv} ('%s' given)", new_value);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateRedundancy) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.redundancy must be a positive integer ('%s' given)", new_value);
		return FAILURE;
	}

	return OnUpdateLong(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
}
/* }}} */

/* {{{ PHP_INI */
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("memcache.allow_failover",		"1",		PHP_INI_ALL, OnUpdateLong,			allow_failover,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.max_failover_attempts",	"20",		PHP_INI_ALL, OnUpdateFailoverAttempts,		max_failover_attempts,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.default_port",			"11211",	PHP_INI_ALL, OnUpdateLong,			default_port,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.chunk_size",			"8192",		PHP_INI_ALL, OnUpdateChunkSize,		chunk_size,		zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.hash_strategy",			"standard",	PHP_INI_ALL, OnUpdateHashStrategy,	hash_strategy,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.hash_function",			"crc32",	PHP_INI_ALL, OnUpdateHashFunction,	hash_function,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.redundancy",			"1",		PHP_INI_ALL, OnUpdateRedundancy,	redundancy,			zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.session_redundancy",	"1",		PHP_INI_ALL, OnUpdateRedundancy,	session_redundancy,	zend_memcache_globals,	memcache_globals)
PHP_INI_END()
/* }}} */

/* {{{ macros */
#define MMC_PREPARE_KEY(key, key_len) \
	php_strtr(key, key_len, "\t\r\n ", "____", 4); \
/* }}} */

/* {{{ internal function protos */
static void _mmc_pool_list_dtor(zend_rsrc_list_entry * TSRMLS_DC);
static void _mmc_server_list_dtor(zend_rsrc_list_entry * TSRMLS_DC);
static void php_mmc_set_failure_callback(mmc_pool_t *, zval *, zval * TSRMLS_DC);
/* }}} */

/* {{{ php_memcache_init_globals()
*/
static void php_memcache_init_globals(zend_memcache_globals *memcache_globals_p TSRMLS_DC)
{
	MEMCACHE_G(hash_strategy)	  = MMC_STANDARD_HASH;
	MEMCACHE_G(hash_function)	  = MMC_HASH_CRC32;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(memcache)
{
	zend_class_entry ce;
	
	INIT_CLASS_ENTRY(ce, "MemcachePool", php_memcache_pool_class_functions);
	memcache_pool_ce = zend_register_internal_class(&ce TSRMLS_CC);
	
	INIT_CLASS_ENTRY(ce, "Memcache", php_memcache_class_functions);
	memcache_ce = zend_register_internal_class_ex(&ce, memcache_pool_ce, NULL TSRMLS_CC);

	le_memcache_pool = zend_register_list_destructors_ex(_mmc_pool_list_dtor, NULL, "memcache connection", module_number);
	le_memcache_server = zend_register_list_destructors_ex(NULL, _mmc_server_list_dtor, "persistent memcache connection", module_number);

#ifdef ZTS
	ts_allocate_id(&memcache_globals_id, sizeof(zend_memcache_globals), (ts_allocate_ctor) php_memcache_init_globals, NULL);
#else
	php_memcache_init_globals(&memcache_globals TSRMLS_CC);
#endif

	REGISTER_LONG_CONSTANT("MEMCACHE_COMPRESSED", MMC_COMPRESSED, CONST_CS | CONST_PERSISTENT);
	REGISTER_INI_ENTRIES();

#if HAVE_MEMCACHE_SESSION
	REGISTER_LONG_CONSTANT("MEMCACHE_HAVE_SESSION", 1, CONST_CS | CONST_PERSISTENT);
	php_session_register_module(ps_memcache_ptr);
#else
	REGISTER_LONG_CONSTANT("MEMCACHE_HAVE_SESSION", 0, CONST_CS | CONST_PERSISTENT);
#endif

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(memcache)
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(memcache)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "memcache support", "enabled");
	php_info_print_table_row(2, "Revision", "$Revision$");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* ------------------
   internal functions
   ------------------ */

static void _mmc_pool_list_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	mmc_pool_free((mmc_pool_t *)rsrc->ptr TSRMLS_CC);
}
/* }}} */

static void _mmc_server_list_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	mmc_server_free((mmc_t *)rsrc->ptr TSRMLS_CC);
}
/* }}} */

static int mmc_get_pool(zval *id, mmc_pool_t **pool TSRMLS_DC) /* {{{ */
{
	zval **connection;
	int resource_type;

	if (Z_TYPE_P(id) != IS_OBJECT || zend_hash_find(Z_OBJPROP_P(id), "connection", sizeof("connection"), (void **)&connection) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to extract 'connection' variable from object");
		return 0;
	}

	*pool = (mmc_pool_t *) zend_list_find(Z_LVAL_PP(connection), &resource_type);

	if (!*pool || resource_type != le_memcache_pool) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown connection identifier");
		return 0;
	}

	return Z_LVAL_PP(connection);
}
/* }}} */

int mmc_stored_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses a SET/ADD/REPLACE response line, param is a zval pointer to store result into {{{ */
{
	if (mmc_str_left((char *)value, "STORED", value_len, sizeof("STORED")-1)) {
		if (param != NULL && Z_TYPE_P((zval *)param) == IS_NULL) {
			ZVAL_TRUE((zval *)param);
		}
		return MMC_REQUEST_DONE;
	}

	/* return FALSE or catch memory errors without failover */
	if (mmc_str_left((char *)value, "NOT_STORED", value_len, sizeof("NOT_STORED")-1) ||
		mmc_str_left((char *)value, "SERVER_ERROR out of memory", value_len, sizeof("SERVER_ERROR out of memory")-1) ||
		mmc_str_left((char *)value, "SERVER_ERROR object too large", value_len, sizeof("SERVER_ERROR object too large")-1)) {
		if (param != NULL) {
			ZVAL_FALSE((zval *)param);
		}
		return MMC_REQUEST_DONE;
	}

	return mmc_request_failure(mmc, request->io, (char *)value, value_len, 0 TSRMLS_CC);
}
/* }}} */

static void php_mmc_store(INTERNAL_FUNCTION_PARAMETERS, char *cmd, int cmd_len) /* {{{ */
{
	mmc_pool_t *pool;
	mmc_request_t *request;
	zval *keys, *value, *mmc_object = getThis();
	long flags = 0, exptime = 0;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|zll", &mmc_object, memcache_pool_ce, &keys, &value, &flags, &exptime) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|zll", &keys, &value, &flags, &exptime) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}

	RETVAL_NULL();

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		zstr key;
		char keytmp[MAX_LENGTH_OF_LONG + 1];
		unsigned int key_len;
		unsigned long index;
		int key_type;
		
		zval **arrval;
		HashPosition pos;
		zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(keys), &pos);
		
		while (zend_hash_get_current_data_ex(Z_ARRVAL_P(keys), (void **)&arrval, &pos) == SUCCESS) {
			key_type = zend_hash_get_current_key_ex(Z_ARRVAL_P(keys), &key, &key_len, &index, 0, &pos);
			zend_hash_move_forward_ex(Z_ARRVAL_P(keys), &pos);
			
			switch (key_type) {
				case HASH_KEY_IS_STRING:
					key_len--;
					break;

				case HASH_KEY_IS_LONG:
					key_len = sprintf(keytmp, "%lu", index);
					key = ZSTR(keytmp);
					break;

				default:
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid key");
					continue;
			}

			/* allocate request */
			if (return_value_used) {
				request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
					mmc_stored_handler, return_value, mmc_pool_failover_handler, NULL TSRMLS_CC);
			}
			else {
				request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
					mmc_stored_handler, NULL, mmc_pool_failover_handler, NULL TSRMLS_CC);
			}
			
			/* assemble command */
			if (mmc_prepare_store(pool, request, cmd, cmd_len, ZSTR_VAL(key), key_len, flags, exptime, *arrval TSRMLS_CC) != MMC_OK) {
				mmc_pool_release(pool, request);
				continue;
			}
			
			/* schedule request */
			if (mmc_pool_schedule_key(pool, request->key, request->key_len, request, MEMCACHE_G(redundancy) TSRMLS_CC) != MMC_OK) {
				continue;
			}

			/* begin sending requests immediatly */
			mmc_pool_select(pool, 0 TSRMLS_CC);
		}
	}
	else {
		/* allocate request */
		request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
			mmc_stored_handler, return_value, mmc_pool_failover_handler, NULL TSRMLS_CC);

		/* assemble command */
		if (mmc_prepare_store(pool, request, cmd, cmd_len, Z_STRVAL_P(keys), Z_STRLEN_P(keys), flags, exptime, value TSRMLS_CC) != MMC_OK) {
			mmc_pool_release(pool, request);
			RETURN_FALSE;
		}
		
		/* schedule request */
		if (mmc_pool_schedule_key(pool, request->key, request->key_len, request, MEMCACHE_G(redundancy) TSRMLS_CC) != MMC_OK) {
			RETURN_FALSE;
		}
	}
	
	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);

	if (Z_TYPE_P(return_value) == IS_NULL) {
		RETVAL_FALSE;
	}
}
/* }}} */

static int mmc_deleted_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses a DELETED response line, param is a zval pointer to store result into {{{ */
{
	if (mmc_str_left((char *)value, "DELETED", value_len, sizeof("DELETED")-1)) {
		if (param != NULL && Z_TYPE_P((zval *)param) == IS_NULL) {
			ZVAL_TRUE((zval *)param);
		}
		return MMC_REQUEST_DONE;
	}
	
	if (mmc_str_left((char *)value, "NOT_FOUND", value_len, sizeof("NOT_FOUND")-1)) {
		if (param != NULL) {
			ZVAL_FALSE((zval *)param);
		}
		return MMC_REQUEST_DONE;
	}
	
	return mmc_request_failure(mmc, request->io, (char *)value, value_len, 0 TSRMLS_CC);
}
/* }}} */

static int mmc_numeric_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses a numeric response line, param is a zval pointer to store result into {{{ */
{
	/* must contain digit(s) + \r\n */
	if (value_len < 3) {
		return mmc_request_failure(mmc, request->io, (char *)value, value_len, 0 TSRMLS_CC);
	}
	
	/* append return value to result array */
	if (param != NULL) {
		if (Z_TYPE_P((zval *)param) == IS_NULL) {
			array_init((zval *)param);
		}
		
		if (Z_TYPE_P((zval *)param) == IS_ARRAY) {
			zval *result;
			MAKE_STD_ZVAL(result);
			ZVAL_LONG(result, atol((char *)value));
			add_assoc_zval_ex((zval *)param, request->key, request->key_len + 1, result);
		}
		else {
			ZVAL_LONG((zval *)param, atol((char *)value));
		}
	}
	
	return MMC_REQUEST_DONE;
}
/* }}} */

static void php_mmc_numeric(INTERNAL_FUNCTION_PARAMETERS, const char *cmd, unsigned int cmd_len, int deleted) /* 
	sends one or several commands which have a single optional numeric parameter (incr, decr, delete) {{{ */
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	
	zval *keys;
	long value = 1;
	mmc_request_t *request;
	mmc_request_value_handler value_handler;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|l", &mmc_object, memcache_pool_ce, &keys, &value) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|l", &keys, &value) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}
	
	if (deleted) {
		value_handler = mmc_deleted_handler;
	}
	else {
		value_handler = mmc_numeric_handler;
	}

	if (Z_TYPE_P(keys) == IS_ARRAY) {
		zval **key;
		HashPosition pos;
		zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(keys), &pos);

		RETVAL_NULL();
		
		while (zend_hash_get_current_data_ex(Z_ARRVAL_P(keys), (void **)&key, &pos) == SUCCESS) {
			zend_hash_move_forward_ex(Z_ARRVAL_P(keys), &pos);

			/* allocate request */
			if (return_value_used) {
				request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
					value_handler, return_value, mmc_pool_failover_handler, NULL TSRMLS_CC);
			}
			else {
				request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
					value_handler, NULL, mmc_pool_failover_handler, NULL TSRMLS_CC);
			}

			if (mmc_prepare_key(*key, request->key, &(request->key_len)) != MMC_OK) {
				mmc_pool_release(pool, request);
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid key");
				continue;
			}
			
			smart_str_appendl(&(request->sendbuf.value), cmd, cmd_len);
			smart_str_appendl(&(request->sendbuf.value), " ", 1);
			smart_str_appendl(&(request->sendbuf.value), request->key, request->key_len);

			if (value > 0) {
				smart_str_appendl(&(request->sendbuf.value), " ", 1);
				smart_str_append_unsigned(&(request->sendbuf.value), value);
			}

			smart_str_appendl(&(request->sendbuf.value), "\r\n", sizeof("\r\n")-1);

			/* schedule request */
			if (mmc_pool_schedule_key(pool, request->key, request->key_len, request, MEMCACHE_G(redundancy) TSRMLS_CC) != MMC_OK) {
				continue;
			}

			/* begin sending requests immediatly */
			mmc_pool_select(pool, 0 TSRMLS_CC);
		}
	}
	else {
		if (deleted) {
			RETVAL_NULL();
		}
		else {
			RETVAL_FALSE;
		}
		
		/* allocate request */
		request = mmc_pool_request(pool, MMC_PROTO_TCP, mmc_request_parse_line, 
			value_handler, return_value, mmc_pool_failover_handler, NULL TSRMLS_CC);

		if (mmc_prepare_key(keys, request->key, &(request->key_len)) != MMC_OK) {
			mmc_pool_release(pool, request);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid key");
			RETURN_FALSE;
		}

		smart_str_appendl(&(request->sendbuf.value), cmd, cmd_len);
		smart_str_appendl(&(request->sendbuf.value), " ", 1);
		smart_str_appendl(&(request->sendbuf.value), request->key, request->key_len);
		
		if (value > 0) {
			smart_str_appendl(&(request->sendbuf.value), " ", 1);
			smart_str_append_unsigned(&(request->sendbuf.value), value);
		}

		smart_str_appendl(&(request->sendbuf.value), "\r\n", sizeof("\r\n")-1);

		/* schedule request */
		if (mmc_pool_schedule_key(pool, request->key, request->key_len, request, MEMCACHE_G(redundancy) TSRMLS_CC) != MMC_OK) {
			RETURN_FALSE;
		}
	}
	
	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);
}
/* }}} */

mmc_t *mmc_find_persistent(const char *host, int host_len, unsigned short port, unsigned short udp_port, int timeout, int retry_interval TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	zend_rsrc_list_entry *le;
	char *key;
	int key_len;

	key_len = spprintf(&key, 0, "memcache:server:%s:%u:%u", host, port, udp_port);
	
	if (zend_hash_find(&EG(persistent_list), key, key_len+1, (void **)&le) == FAILURE) {
		zend_rsrc_list_entry new_le;

		mmc = mmc_server_new(host, host_len, port, udp_port, 1, timeout, retry_interval TSRMLS_CC);
		new_le.type = le_memcache_server;
		new_le.ptr  = mmc;

		/* register new persistent connection */
		if (zend_hash_update(&EG(persistent_list), key, key_len+1, (void *)&new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
			mmc_server_free(mmc TSRMLS_CC);
			mmc = NULL;
		} else {
			zend_list_insert(mmc, le_memcache_server);
		}
	}
	else if (le->type != le_memcache_server || le->ptr == NULL) {
		zend_rsrc_list_entry new_le;
		zend_hash_del(&EG(persistent_list), key, key_len+1);

		mmc = mmc_server_new(host, host_len, port, udp_port, 1, timeout, retry_interval TSRMLS_CC);
		new_le.type = le_memcache_server;
		new_le.ptr  = mmc;

		/* register new persistent connection */
		if (zend_hash_update(&EG(persistent_list), key, key_len+1, (void *)&new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
			mmc_server_free(mmc TSRMLS_CC);
			mmc = NULL;
		}
		else {
			zend_list_insert(mmc, le_memcache_server);
		}
	}
	else {
		mmc = (mmc_t *)le->ptr;
		mmc->timeout = timeout;
		mmc->tcp.retry_interval = retry_interval;

		/* attempt to reconnect this node before failover in case connection has gone away */
		if (mmc->tcp.status == MMC_STATUS_CONNECTED) {
			mmc->tcp.status = MMC_STATUS_UNKNOWN;
		}
		if (mmc->udp.status == MMC_STATUS_CONNECTED) {
			mmc->udp.status = MMC_STATUS_UNKNOWN;
		}
	}

	efree(key);
	return mmc;
}
/* }}} */

static mmc_t *php_mmc_pool_addserver(
	zval *mmc_object, const char *host, int host_len, long tcp_port, long udp_port, long weight, 
	zend_bool persistent, long timeout, long retry_interval, zend_bool status, mmc_pool_t **pool_result TSRMLS_DC) /* {{{ */
{
	zval **connection;
	mmc_pool_t *pool;
	mmc_t *mmc;
	int list_id, resource_type;
	
	if (weight < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "weight must be a positive integer");
		return NULL;
	}

	/* lazy initialization of server struct */
	if (persistent && status) {
		mmc = mmc_find_persistent(host, host_len, tcp_port, udp_port, timeout, retry_interval TSRMLS_CC);
	}
	else {
		mmc = mmc_server_new(host, host_len, tcp_port, udp_port, 0, timeout, retry_interval TSRMLS_CC);
	}

	/* add server in failed mode */
	if (!status) {
		mmc->tcp.status = MMC_STATUS_FAILED;
		mmc->udp.status = MMC_STATUS_FAILED;
	}

	/* initialize pool if need be */
	if (zend_hash_find(Z_OBJPROP_P(mmc_object), "connection", sizeof("connection"), (void **)&connection) == FAILURE) {
		pool = mmc_pool_new(TSRMLS_C);
		list_id = zend_list_insert(pool, le_memcache_pool);
		add_property_resource(mmc_object, "connection", list_id);
	}
	else {
		pool = (mmc_pool_t *)zend_list_find(Z_LVAL_PP(connection), &resource_type);
		if (!pool || resource_type != le_memcache_pool) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown connection identifier");
			return NULL;
		}
	}

	mmc_pool_add(pool, mmc, weight);
	
	if (pool_result != NULL) {
		*pool_result = pool;
	}
	
	return mmc;
	
}
/* }}} */

static void php_mmc_connect(INTERNAL_FUNCTION_PARAMETERS, zend_bool persistent) /* {{{ */
{
	zval *mmc_object = getThis();
	mmc_pool_t *pool;
	mmc_t *mmc;

	char *host;
	int host_len;
	long tcp_port = 0, timeout = MMC_DEFAULT_TIMEOUT;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|ll", &host, &host_len, &tcp_port, &timeout) == FAILURE) {
		return;
	}

	/* initialize pool and object if need be */
	if (!mmc_object) {
		int list_id = zend_list_insert(mmc_pool_new(TSRMLS_C), le_memcache_pool);
		mmc_object = return_value;
		object_init_ex(mmc_object, memcache_ce);
		add_property_resource(mmc_object, "connection", list_id);
	}
	else {
		RETVAL_TRUE;
	}
	
	mmc = php_mmc_pool_addserver(mmc_object, host, host_len, tcp_port, 0, 1, persistent, timeout, MMC_DEFAULT_RETRY, 1, NULL TSRMLS_CC);
	if (mmc == NULL) {
		RETURN_FALSE;
	}

	/* force a reconnect attempt if stream EOF */
	if (mmc->tcp.stream != NULL && php_stream_eof(mmc->tcp.stream)) {
		mmc_server_disconnect(mmc, &(mmc->tcp) TSRMLS_CC);
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}
	
	/* force a tcp connect (if not persistently connected) */
	if (mmc_pool_open(pool, mmc, &(mmc->tcp), 0 TSRMLS_CC) != MMC_OK) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't connect to %s:%d, %s (%d)", host, mmc->tcp.port, mmc->error ? mmc->error : "Unknown error", mmc->errnum);
		RETURN_FALSE;
	}
}
/* }}} */

/*
 * STAT 6:chunk_size 64
 */
static int mmc_stats_parse_stat(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *colon, *key;
	long index = 0;

	if (Z_TYPE_P(result) != IS_ARRAY) {
		array_init(result);
	}

	/* find space delimiting key and value */
	if ((space = php_memnstr(start, " ", 1, end)) == NULL) {
		return 0;
	}

	/* find colon delimiting subkeys */
	if ((colon = php_memnstr(start, ":", 1, space - 1)) != NULL) {
		zval *element, **elem;
		key = estrndup(start, colon - start);

		/* find existing or create subkey array in result */
		if ((is_numeric_string(key, colon - start, &index, NULL, 0) &&
			zend_hash_index_find(Z_ARRVAL_P(result), index, (void **)&elem) != FAILURE) ||
			zend_hash_find(Z_ARRVAL_P(result), key, colon - start + 1, (void **)&elem) != FAILURE) {
			element = *elem;
		}
		else {
			MAKE_STD_ZVAL(element);
			array_init(element);
			add_assoc_zval_ex(result, key, colon - start + 1, element);
		}

		efree(key);
		return mmc_stats_parse_stat(colon + 1, end, element TSRMLS_CC);
	}

	/* no more subkeys, add value under last subkey */
	key = estrndup(start, space - start);
	add_assoc_stringl_ex(result, key, space - start + 1, space + 1, end - space, 1);
	efree(key);

	return 1;
}
/* }}} */

/*
 * ITEM test_key [3 b; 1157099416 s]
 */
static int mmc_stats_parse_item(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *value, *value_end, *key;
	zval *element;

	if (Z_TYPE_P(result) != IS_ARRAY) {
		array_init(result);
	}

	/* find space delimiting key and value */
	if ((space = php_memnstr(start, " ", 1, end)) == NULL) {
		return 0;
	}

	MAKE_STD_ZVAL(element);
	array_init(element);

	/* parse each contained value */
	for (value = php_memnstr(space, "[", 1, end); value != NULL && value <= end; value = php_memnstr(value + 1, ";", 1, end)) {
		do {
			value++;
		} while (*value == ' ' && value <= end);

		if (value <= end && (value_end = php_memnstr(value, " ", 1, end)) != NULL && value_end <= end) {
			add_next_index_stringl(element, value, value_end - value, 1);
		}
	}

	/* add parsed values under key */
	key = estrndup(start, space - start);
	add_assoc_zval_ex(result, key, space - start + 1, element);
	efree(key);

	return 1;
}
/* }}} */

static int mmc_stats_parse_generic(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *key;

	if (Z_TYPE_P(result) != IS_ARRAY) {
		array_init(result);
	}

	/* "stats maps" returns "\n" delimited lines, other commands uses "\r\n" */
	if (*end == '\r') {
		end--;
	}

	if (start <= end) {
		if ((space = php_memnstr(start, " ", 1, end)) != NULL) {
			key = estrndup(start, space - start);
			add_assoc_stringl_ex(result, key, space - start + 1, space + 1, end - space, 1);
			efree(key);
		}
		else {
			add_next_index_stringl(result, start, end - start, 1);
		}
	}

	return 1;
}
/* }}} */

static void php_mmc_failure_callback(mmc_pool_t *pool, mmc_t *mmc, void *param TSRMLS_DC)  /* {{{ */ 
{
	zval *mmc_object = (zval *)param;
	zval **callback;
	
	/* check for userspace callback */
	if (zend_hash_find(Z_OBJPROP_P(mmc_object), "failed", sizeof("failed"), (void **)&callback) == SUCCESS && Z_TYPE_PP(callback) != IS_NULL) {
		if (zend_is_callable(*callback, 0, NULL)) {
			zval *retval;
			zval *host, *tcp_port, *udp_port, *error, *errnum;
			zval **params[5] = {&host, &tcp_port, &udp_port, &error, &errnum};

			MAKE_STD_ZVAL(host);
			MAKE_STD_ZVAL(tcp_port); MAKE_STD_ZVAL(udp_port);
			MAKE_STD_ZVAL(error); MAKE_STD_ZVAL(errnum);

			ZVAL_STRING(host, mmc->host, 1);
			ZVAL_LONG(tcp_port, mmc->tcp.port); ZVAL_LONG(udp_port, mmc->udp.port);
			
			if (mmc->error != NULL) {
				ZVAL_STRING(error, mmc->error, 1);
			}
			else {
				ZVAL_NULL(error);
			}
			ZVAL_LONG(errnum, mmc->errnum);

			call_user_function_ex(EG(function_table), NULL, *callback, &retval, 5, params, 0, NULL TSRMLS_CC);

			zval_ptr_dtor(&host);
			zval_ptr_dtor(&tcp_port); zval_ptr_dtor(&udp_port);
			zval_ptr_dtor(&error); zval_ptr_dtor(&errnum);
			zval_ptr_dtor(&retval);
		}
		else {
			php_mmc_set_failure_callback(pool, mmc_object, NULL TSRMLS_CC);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid failure callback");
		}
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Server %s (tcp %d, udp %d) failed with: %s (%d)", 
			mmc->host, mmc->tcp.port, mmc->udp.port, mmc->error, mmc->errnum);
	}
}
/* }}} */

static void php_mmc_set_failure_callback(mmc_pool_t *pool, zval *mmc_object, zval *callback TSRMLS_DC)  /* {{{ */
{
	if (callback != NULL) {
		add_property_zval(mmc_object, "failed", callback);	
		pool->failure_callback = &php_mmc_failure_callback;
		pool->failure_callback_param = mmc_object;  
	}
	else {
		add_property_null(mmc_object, "failed");	
		pool->failure_callback = NULL;
		pool->failure_callback_param = NULL;  
	}
}
/* }}} */

/* ----------------
   module functions
   ---------------- */

/* {{{ proto bool MemcachePool::connect(string host [, int tcp_port [, int udp_port [, bool persistent [, int weight [, int timeout [, int retry_interval] ] ] ] ] ])
   Connects to server and returns a Memcache object */
PHP_NAMED_FUNCTION(zif_memcache_pool_connect)
{
	zval *mmc_object = getThis();
	mmc_pool_t *pool;
	mmc_t *mmc;

	char *host;
	int host_len;
	long tcp_port = 0, udp_port = 0, weight = 1, timeout = MMC_DEFAULT_TIMEOUT, retry_interval = MMC_DEFAULT_RETRY;
	zend_bool persistent = 1;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|llblll", 
		&host, &host_len, &tcp_port, &udp_port, &persistent, &weight, &timeout, &retry_interval) == FAILURE) {
		return;
	}
	
	mmc = php_mmc_pool_addserver(mmc_object, host, host_len, tcp_port, udp_port, weight, persistent, timeout, retry_interval, 1, NULL TSRMLS_CC);
	if (mmc == NULL) {
		RETURN_FALSE;
	}
	
	/* force a reconnect attempt if stream EOF */
	if (mmc->tcp.stream != NULL && php_stream_eof(mmc->tcp.stream)) {
		mmc_server_disconnect(mmc, &(mmc->tcp) TSRMLS_CC);
	}
	
	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}
	
	/* force a tcp connect (if not persistently connected) */
	if (mmc_pool_open(pool, mmc, &(mmc->tcp), 0 TSRMLS_CC) != MMC_OK) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't connect to %s:%d, %s (%d)", host, mmc->tcp.port, mmc->error ? mmc->error : "Unknown error", mmc->errnum);
		RETURN_FALSE;
	}
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto object memcache_connect(string host [, int port [, int timeout ] ])
   Connects to server and returns a Memcache object */
PHP_FUNCTION(memcache_connect)
{
	php_mmc_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ proto object memcache_pconnect(string host [, int port [, int timeout ] ])
   Connects to server and returns a Memcache object */
PHP_FUNCTION(memcache_pconnect)
{
	php_mmc_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */

/* {{{ proto bool MemcachePool::addServer(string host [, int tcp_port [, int udp_port [, bool persistent [, int weight [, int timeout [, int retry_interval [, bool status] ] ] ] ])
   Adds a server to the pool */
PHP_NAMED_FUNCTION(zif_memcache_pool_addserver)
{
	zval *mmc_object = getThis();
	mmc_t *mmc;

	char *host;
	int host_len;
	long tcp_port = 0, udp_port = 0, weight = 1, timeout = MMC_DEFAULT_TIMEOUT, retry_interval = MMC_DEFAULT_RETRY;
	zend_bool persistent = 1, status = 1;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|llblllb", 
		&host, &host_len, &tcp_port, &udp_port, &persistent, &weight, &timeout, &retry_interval, &status) == FAILURE) {
		return;
	}
	
	mmc = php_mmc_pool_addserver(mmc_object, host, host_len, tcp_port, udp_port, weight, persistent, timeout, retry_interval, status, NULL TSRMLS_CC);
	if (mmc == NULL) {
		RETURN_FALSE;
	}
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_add_server(string host [, int port [, bool persistent [, int weight [, int timeout [, int retry_interval [, bool status [, callback failure_callback ] ] ] ] ] ] ])
   Adds a connection to the pool. The order in which this function is called is significant */
PHP_FUNCTION(memcache_add_server)
{
	zval *mmc_object = getThis(), *failure_callback = NULL;
	mmc_pool_t *pool;
	mmc_t *mmc;

	char *host;
	int host_len;
	long tcp_port = MEMCACHE_G(default_port), weight = 1, timeout = MMC_DEFAULT_TIMEOUT, retry_interval = MMC_DEFAULT_RETRY;
	zend_bool persistent = 1, status = 1;

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lblllbz", 
			&host, &host_len, &tcp_port, &persistent, &weight, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|lblllbz", &mmc_object, memcache_ce, 
			&host, &host_len, &tcp_port, &persistent, &weight, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			return;
		}
	}

	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		if (!zend_is_callable(failure_callback, 0, NULL)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid failure callback");
			RETURN_FALSE;
		}
	}

	mmc = php_mmc_pool_addserver(mmc_object, host, host_len, tcp_port, 0, weight, persistent, timeout, retry_interval, status, &pool TSRMLS_CC);
	if (mmc == NULL) {
		RETURN_FALSE;
	}
	
	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		php_mmc_set_failure_callback(pool, mmc_object, failure_callback TSRMLS_CC);
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_set_server_params( string host [, int port [, int timeout [, int retry_interval [, bool status [, callback failure_callback ] ] ] ] ])
   Changes server parameters at runtime */
PHP_FUNCTION(memcache_set_server_params)
{
	zval *mmc_object = getThis(), *failure_callback = NULL;
	mmc_pool_t *pool;
	mmc_t *mmc = NULL;
	long tcp_port = MEMCACHE_G(default_port), timeout = MMC_DEFAULT_TIMEOUT, retry_interval = MMC_DEFAULT_RETRY;
	zend_bool status = 1;
	int host_len, i;
	char *host;

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lllbz", 
			&host, &host_len, &tcp_port, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|lllbz", &mmc_object, memcache_pool_ce, 
			&host, &host_len, &tcp_port, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}

	for (i=0; i<pool->num_servers; i++) {
		if (!strcmp(pool->servers[i]->host, host) && pool->servers[i]->tcp.port == tcp_port) {
			mmc = pool->servers[i];
			break;
		}
	}

	if (!mmc) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Server not found in pool");
		RETURN_FALSE;
	}

	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		if (!zend_is_callable(failure_callback, 0, NULL)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid failure callback");
			RETURN_FALSE;
		}
	}

	mmc->timeout = timeout;
	mmc->tcp.retry_interval = retry_interval;

	if (!status) {
		mmc->tcp.status = MMC_STATUS_FAILED;
		mmc->udp.status = MMC_STATUS_FAILED;
	}
	else {
		if (mmc->tcp.status == MMC_STATUS_FAILED) {
			mmc->tcp.status = MMC_STATUS_DISCONNECTED;
		}
		if (mmc->udp.status == MMC_STATUS_FAILED) {
			mmc->udp.status = MMC_STATUS_DISCONNECTED;
		}
	}

	if (failure_callback != NULL) {
		if (Z_TYPE_P(failure_callback) != IS_NULL) {
			php_mmc_set_failure_callback(pool, mmc_object, failure_callback TSRMLS_CC);
		}
		else {
			php_mmc_set_failure_callback(pool, mmc_object, NULL TSRMLS_CC);
		}
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto int memcache_get_server_status( string host [, int port ])
   Returns server status (0 if server is failed, otherwise non-zero) */
PHP_FUNCTION(memcache_get_server_status)
{
	zval *mmc_object = getThis();
	mmc_pool_t *pool;
	mmc_t *mmc = NULL;
	long tcp_port = MEMCACHE_G(default_port);
	int host_len, i;
	char *host;

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &host, &host_len, &tcp_port) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|l", &mmc_object, memcache_pool_ce, &host, &host_len, &tcp_port) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}

	for (i=0; i<pool->num_servers; i++) {
		if (!strcmp(pool->servers[i]->host, host) && pool->servers[i]->tcp.port == tcp_port) {
			mmc = pool->servers[i];
			break;
		}
	}

	if (!mmc) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Server not found in pool");
		RETURN_FALSE;
	}

	RETURN_LONG(mmc->tcp.status > MMC_STATUS_FAILED ? 1 : 0);
}
/* }}} */

static int mmc_version_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses the VERSION response line, param is a zval pointer to store version into {{{ */
{
	unsigned int version_len = value_len - (sizeof("VERSION ")-1) - (sizeof("\r\n")-1);
	char *version = emalloc(version_len + 1);
	
	if (sscanf((char *)value, "VERSION %s", version) != 1) {
		efree(version);
		return mmc_request_failure(mmc, request->io, (char *)value, value_len, 0 TSRMLS_CC);
	}
	
	ZVAL_STRINGL((zval *)param, version, version_len, 0);
	return MMC_REQUEST_DONE;
}
/* }}} */

/* {{{ proto string memcache_get_version( object memcache )
   Returns server's version */
PHP_FUNCTION(memcache_get_version)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	int i;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &mmc_object, memcache_pool_ce) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}

	RETVAL_FALSE;
	for (i=0; i<pool->num_servers; i++) {
		/* run command and check for valid return value */
		if (mmc_pool_schedule_command(pool, pool->servers[i], "version\r\n", sizeof("version\r\n")-1, 
			mmc_request_parse_line, mmc_version_handler, return_value TSRMLS_CC) == MMC_OK) {
			mmc_pool_run(pool TSRMLS_CC);
			if (Z_TYPE_P(return_value) == IS_STRING) {
				break;
			}
		}
	}
}
/* }}} */

/* {{{ proto bool memcache_add(object memcache, mixed key [, mixed var [, int flag [, int exptime ] ] ])
   Adds new item. Item with such key should not exist. */
PHP_FUNCTION(memcache_add)
{
	php_mmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "add", sizeof("add")-1);
}
/* }}} */

/* {{{ proto bool memcache_set(object memcache, mixed key [, mixed var [, int flag [, int exptime ] ] ])
   Sets the value of an item. Item may exist or not */
PHP_FUNCTION(memcache_set)
{
	php_mmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "set", sizeof("set")-1);
}
/* }}} */

/* {{{ proto bool memcache_replace(object memcache, mixed key [, mixed var [, int flag [, int exptime ] ] )
   Replaces existing item. Returns false if item doesn't exist */
PHP_FUNCTION(memcache_replace)
{
	php_mmc_store(INTERNAL_FUNCTION_PARAM_PASSTHRU, "replace", sizeof("replace")-1);
}
/* }}} */

static int mmc_value_handler_multi(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	receives a multiple values, param is a zval pointer array to store value in {{{ */
{
	zval *arrval;
	ALLOC_ZVAL(arrval);
	*((zval *)arrval) = *((zval *)value);
	
	if (Z_TYPE_P((zval *)param) != IS_ARRAY) {
		array_init((zval *)param);
	}
	add_assoc_zval_ex((zval *)param, request->value.key, request->value.key_len + 1, arrval);

	/* request more data (more values or END line) */
	return MMC_REQUEST_AGAIN;
}
/* }}} */

int mmc_value_handler_single(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	receives a single value, param is a zval pointer to store value to {{{ */
{
	*((zval *)param) = *((zval *)value);

	/* request more data (END line) */
	return MMC_REQUEST_AGAIN;
}
/* }}} */

static int mmc_value_failover_handler(mmc_pool_t *pool, mmc_t *mmc, mmc_request_t *request, void *param TSRMLS_DC) /* 
	uses keys and return value to reschedule requests to other servers, param is a zval ** pointer {{{ */
{
	char keytmp[MMC_MAX_KEY_LEN + 1];
	unsigned int keytmp_len;
	
	zval **key, *keys = ((zval **)param)[0], *return_value = ((zval **)param)[0];
	HashPosition pos;

	if (!MEMCACHE_G(allow_failover) || request->failed_servers.len >= MEMCACHE_G(max_failover_attempts)) {
		mmc_pool_release(pool, request);
		return MMC_REQUEST_FAILURE;
	}
	
	zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(keys), &pos);
	
	while (zend_hash_get_current_data_ex(Z_ARRVAL_P(keys), (void **)&key, &pos) == SUCCESS) {
		zend_hash_move_forward_ex(Z_ARRVAL_P(keys), &pos);
		
		if (mmc_prepare_key(*key, keytmp, &keytmp_len) != MMC_OK) {
			continue;
		}

		/* re-schedule key if it does not exist in return value array */
		if (!zend_hash_exists(Z_ARRVAL_P(return_value), keytmp, keytmp_len)) {
			mmc_pool_schedule_get(pool, MMC_PROTO_UDP, keytmp, keytmp_len,
				mmc_value_handler_multi, return_value, 
				mmc_value_failover_handler, param, request TSRMLS_CC);
		}
	}

	mmc_pool_release(pool, request);
	return MMC_OK;
}
/* }}}*/

/* {{{ proto mixed memcache_get( object memcache, mixed key )
   Returns value of existing item or false */
PHP_FUNCTION(memcache_get)
{
	mmc_pool_t *pool;
	zval *failover_handler_param[2];
	zval *keys, *mmc_object = getThis();

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz", &mmc_object, memcache_pool_ce, &keys) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &keys) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}

	ZVAL_FALSE(return_value);
	
	if (Z_TYPE_P(keys) == IS_ARRAY) {
		char keytmp[MMC_MAX_KEY_LEN + 1];
		unsigned int keytmp_len;
		
		zval **key;
		HashPosition pos;

		zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(keys), &pos);

		failover_handler_param[0] = keys;
		failover_handler_param[1] = return_value;
		
		while (zend_hash_get_current_data_ex(Z_ARRVAL_P(keys), (void **)&key, &pos) == SUCCESS) {
			zend_hash_move_forward_ex(Z_ARRVAL_P(keys), &pos);
			
			if (mmc_prepare_key(*key, keytmp, &keytmp_len) != MMC_OK) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid key");
				continue;
			}
			
			/* schedule request */
			mmc_pool_schedule_get(pool, MMC_PROTO_UDP, keytmp, keytmp_len,
				mmc_value_handler_multi, return_value, 
				mmc_value_failover_handler, failover_handler_param, NULL TSRMLS_CC);
		}
	}
	else {
		mmc_request_t *request;
		
		/* allocate request */
		request = mmc_pool_request(pool, MMC_PROTO_UDP, mmc_request_parse_value, 
			mmc_value_handler_single, return_value, mmc_pool_failover_handler, NULL TSRMLS_CC);

		if (mmc_prepare_key(keys, request->key, &(request->key_len)) != MMC_OK) {
			mmc_pool_release(pool, request);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid key");
			return;
		}

		smart_str_appendl(&(request->sendbuf.value), "get ", sizeof("get ")-1);
		smart_str_appendl(&(request->sendbuf.value), request->key, request->key_len);
		smart_str_appendl(&(request->sendbuf.value), "\r\n", sizeof("\r\n")-1);

		/* schedule request */
		if (mmc_pool_schedule_key(pool, request->key, request->key_len, request, 1 TSRMLS_CC) != MMC_OK) {
			return;
		}
	}
	
	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);
}
/* }}} */

static int mmc_stats_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses the stats response line, param is a zval pointer to store stats into {{{ */
{
	if (!mmc_str_left((char *)value, "ERROR", value_len, sizeof("ERROR")-1) &&
		!mmc_str_left((char *)value, "CLIENT_ERROR", value_len, sizeof("CLIENT_ERROR")-1) &&
		!mmc_str_left((char *)value, "SERVER_ERROR", value_len, sizeof("SERVER_ERROR")-1)) 
	{
		if (mmc_str_left((char *)value, "RESET", value_len, sizeof("RESET")-1)) {
			ZVAL_TRUE((zval *)param);
			return MMC_REQUEST_DONE;
		}
		else if (mmc_str_left((char *)value, "STAT ", value_len, sizeof("STAT ")-1)) {
			if (mmc_stats_parse_stat((char *)value + sizeof("STAT ")-1, (char *)value + value_len - sizeof("\r\n"), (zval *)param TSRMLS_CC)) {
				return MMC_REQUEST_AGAIN;
			}
		}
		else if (mmc_str_left((char *)value, "ITEM ", value_len, sizeof("ITEM ")-1)) {
			if (mmc_stats_parse_item((char *)value + sizeof("ITEM ")-1, (char *)value + value_len - sizeof("\r\n"), (zval *)param TSRMLS_CC)) {
				return MMC_REQUEST_AGAIN;
			}
		}
		else if (mmc_str_left((char *)value, "END", value_len, sizeof("END")-1)) {
			return MMC_REQUEST_DONE;
		}
		else if (mmc_stats_parse_generic((char *)value, (char *)value + value_len - sizeof("\n"), (zval *)param TSRMLS_CC)) {
			return MMC_REQUEST_AGAIN;
		}
	}

	ZVAL_FALSE((zval *)param);
	return MMC_REQUEST_FAILURE;
}
/* }}} */

static int mmc_stats_checktype(const char *type) { /* {{{ */
	return type == NULL ||
		!strcmp(type, "reset") || 
		!strcmp(type, "malloc") || 
		!strcmp(type, "slabs") ||
		!strcmp(type, "cachedump") || 
		!strcmp(type, "items") || 
		!strcmp(type, "sizes");	
}
/* }}} */

/* {{{ proto array memcache_get_stats( object memcache [, string type [, int slabid [, int limit ] ] ])
   Returns server's statistics */
PHP_FUNCTION(memcache_get_stats)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();

	char *cmd, *type = NULL;
	int i, cmd_len, type_len = 0;
	long slabid = 0, limit = MMC_DEFAULT_CACHEDUMP_LIMIT;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|sll", &mmc_object, memcache_pool_ce, &type, &type_len, &slabid, &limit) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|sll", &type, &type_len, &slabid, &limit) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}
	
	if (!mmc_stats_checktype(type)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid stats type");
		RETURN_FALSE;
	}

	if (slabid) {
		cmd_len = spprintf(&cmd, 0, "stats %s %ld %ld\r\n", type, slabid, limit);
	}
	else if (type) {
		cmd_len = spprintf(&cmd, 0, "stats %s\r\n", type);
	}
	else {
		cmd_len = spprintf(&cmd, 0, "stats\r\n");
	}

	ZVAL_FALSE(return_value);
	
	for (i=0; i<pool->num_servers; i++) {
		/* run command and check for valid return value */
		if (mmc_pool_schedule_command(pool, pool->servers[i], cmd, cmd_len, 
			mmc_request_parse_line, mmc_stats_handler, return_value TSRMLS_CC) == MMC_OK) 
		{
			mmc_pool_run(pool TSRMLS_CC);
			if (Z_TYPE_P(return_value) != IS_BOOL || Z_BVAL_P(return_value)) {
				break;
			}
		}
	}
	
	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);
	efree(cmd);
}
/* }}} */

/* {{{ proto array memcache_get_extended_stats( object memcache [, string type [, int slabid [, int limit ] ] ])
   Returns statistics for each server in the pool */
PHP_FUNCTION(memcache_get_extended_stats)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis(), *stats;

	char *host, *cmd, *type = NULL;
	int i, host_len, cmd_len, type_len = 0;
	long slabid = 0, limit = MMC_DEFAULT_CACHEDUMP_LIMIT;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|sll", &mmc_object, memcache_pool_ce, &type, &type_len, &slabid, &limit) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|sll", &type, &type_len, &slabid, &limit) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		RETURN_FALSE;
	}

	if (!mmc_stats_checktype(type)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid stats type");
		RETURN_FALSE;
	}
	
	if (slabid) {
		cmd_len = spprintf(&cmd, 0, "stats %s %ld %ld\r\n", type, slabid, limit);
	}
	else if (type) {
		cmd_len = spprintf(&cmd, 0, "stats %s\r\n", type);
	}
	else {
		cmd_len = spprintf(&cmd, 0, "stats\r\n");
	}

	array_init(return_value);
	
	for (i=0; i<pool->num_servers; i++) {
		MAKE_STD_ZVAL(stats);
		ZVAL_FALSE(stats);

		host_len = spprintf(&host, 0, "%s:%u", pool->servers[i]->host, pool->servers[i]->tcp.port);
		add_assoc_zval_ex(return_value, host, host_len + 1, stats);
		efree(host);

		/* schedule command */
		mmc_pool_schedule_command(pool, pool->servers[i], cmd, cmd_len, 
			mmc_request_parse_line, mmc_stats_handler, stats TSRMLS_CC);

		/* begin sending requests immediatly */
		mmc_pool_select(pool, 0 TSRMLS_CC);
	}

	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);
	efree(cmd);
}
/* }}} */

/* {{{ proto array memcache_set_compress_threshold( object memcache, int threshold [, float min_savings ] )
   Set automatic compress threshold */
PHP_FUNCTION(memcache_set_compress_threshold)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	long threshold;
	double min_savings = MMC_DEFAULT_SAVINGS;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ol|d", &mmc_object, memcache_pool_ce, &threshold, &min_savings) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l|d", &threshold, &min_savings) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}

	if (threshold < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "threshold must be a positive integer");
		RETURN_FALSE;
	}
	pool->compress_threshold = threshold;

	if (min_savings != MMC_DEFAULT_SAVINGS) {
		if (min_savings < 0 || min_savings > 1) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "min_savings must be a float in the 0..1 range");
			RETURN_FALSE;
		}
		pool->min_compress_savings = min_savings;
	}
	else {
		pool->min_compress_savings = MMC_DEFAULT_SAVINGS;
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_delete(object memcache, mixed key [, int exptime ])
   Deletes existing item */
PHP_FUNCTION(memcache_delete)
{
	php_mmc_numeric(INTERNAL_FUNCTION_PARAM_PASSTHRU, "delete", sizeof("delete")-1, 1);
}
/* }}} */

/* {{{ proto mixed memcache_increment(object memcache, mixed key [, int value ])
   Increments existing variable */
PHP_FUNCTION(memcache_increment)
{
	php_mmc_numeric(INTERNAL_FUNCTION_PARAM_PASSTHRU, "incr", sizeof("incr")-1, 0);
}
/* }}} */

/* {{{ proto mixed memcache_decrement(object memcache, mixed key [, int value ])
   Decrements existing variable */
PHP_FUNCTION(memcache_decrement)
{
	php_mmc_numeric(INTERNAL_FUNCTION_PARAM_PASSTHRU, "decr", sizeof("decr")-1, 0);
}
/* }}} */

/* {{{ proto bool memcache_close( object memcache )
   Closes connection to memcached */
PHP_FUNCTION(memcache_close)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	int i;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &mmc_object, memcache_pool_ce) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}

	for (i=0; i<pool->num_servers; i++) {
		if (!pool->servers[i]->persistent) {
			mmc_server_disconnect(pool->servers[i], &(pool->servers[i]->tcp) TSRMLS_CC);
			mmc_server_disconnect(pool->servers[i], &(pool->servers[i]->udp) TSRMLS_CC);
		}
	}

	RETURN_TRUE;
}
/* }}} */

static int mmc_flush_handler(mmc_t *mmc, mmc_request_t *request, void *value, unsigned int value_len, void *param TSRMLS_DC) /* 
	parses the OK response line, param is an int pointer to increment on success {{{ */
{
	if (!mmc_str_left((char *)value, "OK", value_len, sizeof("OK")-1)) {
		return mmc_request_failure(mmc, request->io, (char *)value, value_len, 0 TSRMLS_CC);
	}
	(*((int *)param))++;
	return MMC_REQUEST_DONE;
}
/* }}} */

/* {{{ proto bool memcache_flush( object memcache [, int timestamp ] )
   Flushes cache, optionally at the specified time */
PHP_FUNCTION(memcache_flush)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	
	char cmd[sizeof("flush_all")-1 + 1 + MAX_LENGTH_OF_LONG + sizeof("\r\n")-1 + 1];
	unsigned int cmd_len, i, responses = 0;
	long timestamp = 0;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|l", &mmc_object, memcache_pool_ce, &timestamp) == FAILURE) {
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &timestamp) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		RETURN_FALSE;
	}

	if (timestamp > 0) {
		cmd_len = sprintf(cmd, "flush_all %ld\r\n", timestamp);
	}
	else {
		cmd_len = sprintf(cmd, "flush_all\r\n");
	}

	for (i=0; i<pool->num_servers; i++) {
		mmc_pool_schedule_command(pool, pool->servers[i], cmd, cmd_len, 
			mmc_request_parse_line, mmc_flush_handler, &responses TSRMLS_CC);

		/* begin sending requests immediatly */
		mmc_pool_select(pool, 0 TSRMLS_CC);
	}

	/* execute all requests */
	mmc_pool_run(pool TSRMLS_CC);
	
	if (responses < pool->num_servers) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_debug( bool onoff ) */
PHP_FUNCTION(memcache_debug)
{
	php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache_debug() is deprecated, please use a debugger (like Eclipse + CDT)");
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
