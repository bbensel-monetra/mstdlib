/* The MIT License (MIT)
 * 
 * Copyright (c) 2019 Monetra Technologies, LLC.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "m_config.h"

#include <mstdlib/mstdlib.h>
#include <mstdlib/mstdlib_formats.h>
#include "http/m_http_int.h"

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static M_http_error_t M_http_simple_read_start_cb(M_http_message_type_t type, M_http_version_t version,
	M_http_method_t method, const char *uri, M_uint32 code, const char *reason, void *thunk)
{
	M_http_simple_read_t *simple = thunk;
	M_http_error_t        res    = M_HTTP_ERROR_SUCCESS;

	M_http_set_message_type(simple->http, type);
	M_http_set_version(simple->http, version);

	if (type == M_HTTP_MESSAGE_TYPE_REQUEST) {
		M_http_set_method(simple->http, method);
		if (!M_http_set_uri(simple->http, uri)) {
			res = M_HTTP_ERROR_URI;
		}
	} else {
		M_http_set_status_code(simple->http, code);
		M_http_set_reason_phrase(simple->http, reason);
	}

	return res;
}

static M_http_error_t M_http_simple_read_header_cb(const char *key, const char *val, void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	M_http_set_header_append(simple->http, key, val);
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_header_done_cb(M_http_data_format_t format, void *thunk)
{
	M_http_simple_read_t *simple = thunk;
	const char           *val;
	M_int64               i64v;

	switch (format) {
		case M_HTTP_DATA_FORMAT_NONE:
		case M_HTTP_DATA_FORMAT_BODY:
		case M_HTTP_DATA_FORMAT_CHUNKED:
			break;
		case M_HTTP_DATA_FORMAT_MULTIPART:
		case M_HTTP_DATA_FORMAT_UNKNOWN:
			return M_HTTP_ERROR_UNSUPPORTED_DATA;
	}

	val = M_hash_dict_get_direct(simple->http->headers, "content-length");
	if (M_str_isempty(val) && simple->rflags & M_HTTP_SIMPLE_READ_LEN_REQUIRED) {
		return M_HTTP_ERROR_LENGTH_REQUIRED;
	} else if (!M_str_isempty(val)) {
		if (M_str_to_int64_ex(val, M_str_len(val), 10, &i64v, NULL) != M_STR_INT_SUCCESS || i64v < 0) {
			return M_HTTP_ERROR_CONTENT_LENGTH_MALFORMED;
		}

		/* No body so we're all done. */
		if (i64v == 0) {
			simple->rdone = M_TRUE;
		}

		simple->http->body_len      = (size_t)i64v;
		simple->http->have_body_len = M_TRUE;
	}

	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_body_cb(const unsigned char *data, size_t len, void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	M_http_body_append(simple->http, data, len);

	/* If we don't have a content length and we have a body we can only assume all
	 * the data has been sent in. We only know when we have all data once the connection
	 * is closed. We assume the caller has already received all data. */
	if (!simple->http->have_body_len)
		simple->rdone = M_TRUE;

	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_body_done_cb(void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	simple->rdone = M_TRUE;
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_chunk_extensions_cb(const char *key, const char *val, size_t idx, void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	(void)key;
	(void)val;
	(void)idx;

	if (simple->rflags & M_HTTP_SIMPLE_READ_FAIL_EXTENSION)
		return M_HTTP_ERROR_CHUNK_EXTENSION_NOTALLOWED;
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_chunk_data_cb(const unsigned char *data, size_t len, size_t idx, void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	(void)idx;

	M_http_body_append(simple->http, data, len);
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_chunk_data_finished_cb(void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	simple->rdone = M_TRUE;
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_trailer_cb(const char *key, const char *val, void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	(void)key;
	(void)val;

	if (simple->rflags & M_HTTP_SIMPLE_READ_FAIL_TRAILERS)
		return M_HTTP_ERROR_TRAILER_NOTALLOWED;
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_trailer_done_cb(void *thunk)
{
	M_http_simple_read_t *simple = thunk;

	simple->rdone = M_TRUE;
	return M_HTTP_ERROR_SUCCESS;
}

static M_http_error_t M_http_simple_read_decode_body(M_http_simple_read_t *simple)
{
	char                *dec         = NULL;
	char                 tempa[32];
	/* Note: Default if encoding is not set is M_TEXTCODEC_ISO8859_1 for text.
	 *       We're ignoring this and assuming anything without a charset set
	 *       is binary data. Otherwise, we'd have to detect binary vs text data. */
	M_textcodec_error_t  terr;
	M_bool               update_clen = M_FALSE;

	if (simple == NULL)
		return M_HTTP_ERROR_INVALIDUSE;

	if (simple->rflags & M_HTTP_SIMPLE_READ_NODECODE_BODY)
		return M_HTTP_ERROR_SUCCESS;

	/* Validate we have a content type and a text encoding. */
	if (M_str_isempty(simple->http->content_type) && simple->http->codec == M_TEXTCODEC_UNKNOWN)
		return M_HTTP_ERROR_SUCCESS;

	/* If the Content-Type is form encoded we need to decode. */
	if (M_str_caseeq(simple->http->content_type, "application/x-www-form-urlencoded")) {
		dec  = NULL;
		terr = M_textcodec_decode(&dec, M_buf_peek(simple->http->body), M_TEXTCODEC_EHANDLER_REPLACE, M_TEXTCODEC_PERCENT_FORM);
		M_buf_truncate(simple->http->body, 0);
		M_buf_add_str(simple->http->body, dec);
		M_free(dec);

		if (terr != M_TEXTCODEC_ERROR_SUCCESS && terr != M_TEXTCODEC_ERROR_SUCCESS_EHANDLER) {
			return M_HTTP_ERROR_TEXTCODEC_FAILURE;
		}

		/* There isn't any way to know what the underlying content type is and
		 * there isn't a decoded version of x-www-form-urlencoded so we just
		 * can't set a new mime type. */
		M_http_update_content_type(simple->http, NULL);
		update_clen = M_TRUE;
	}

	/* Decode the data to utf-8 if we can. */
	if (simple->http->codec != M_TEXTCODEC_UNKNOWN && simple->http->codec != M_TEXTCODEC_UTF8) {
		dec  = NULL;
		terr = M_textcodec_decode(&dec, M_buf_peek(simple->http->body), M_TEXTCODEC_EHANDLER_REPLACE, simple->http->codec);
		M_buf_truncate(simple->http->body, 0);
		M_buf_add_str(simple->http->body, dec);
		M_free(dec);

		if (terr != M_TEXTCODEC_ERROR_SUCCESS && terr != M_TEXTCODEC_ERROR_SUCCESS_EHANDLER) {
			return M_HTTP_ERROR_TEXTCODEC_FAILURE;
		}

		M_http_update_charset(simple->http, M_TEXTCODEC_UTF8);
		update_clen = M_TRUE;
	}

	/* We've decoded the data so we need to update the content length. */
	if (update_clen) {
		M_hash_dict_remove(simple->http->headers, "content-length");
		M_snprintf(tempa, sizeof(tempa), "%zu", M_buf_len(simple->http->body));
		M_hash_dict_insert(simple->http->headers, "content-length", tempa);
	}

	return M_HTTP_ERROR_SUCCESS;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static M_http_simple_read_t *M_http_simple_read_create(M_http_simple_read_flags_t flags)
{
	M_http_simple_read_t *simple;

	simple         = M_malloc_zero(sizeof(*simple));
	simple->http   = M_http_create();
	simple->rflags = flags;

	return simple;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void M_http_simple_read_destroy(M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return;

	M_http_destroy(simple->http);
	M_free(simple);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

M_http_message_type_t M_http_simple_read_message_type(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return M_HTTP_MESSAGE_TYPE_UNKNOWN;
	return M_http_message_type(simple->http);
}

M_http_version_t M_http_simple_read_version(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return M_HTTP_VERSION_UNKNOWN;
	return M_http_version(simple->http);
}

M_uint32 M_http_simple_read_status_code(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return 0;
	return M_http_status_code(simple->http);
}

const char *M_http_simple_read_reason_phrase(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_reason_phrase(simple->http);
}

M_http_method_t M_http_simple_read_method(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return M_HTTP_METHOD_UNKNOWN;
	return M_http_method(simple->http);
}

const char *M_http_simple_read_uri(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_uri(simple->http);
}

M_bool M_http_simple_read_port(const M_http_simple_read_t *simple, M_uint16 *port)
{
	M_uint16 myport;

	if (port == NULL)
		port = &myport;
	*port = 0;

	if (simple == NULL)
		return M_FALSE;
	return M_http_port(simple->http, port);
}

const char *M_http_simple_read_path(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_path(simple->http);
}

const char *M_http_simple_read_query_string(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_query_string(simple->http);
}

const M_hash_dict_t *M_http_simple_read_query_args(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_query_args(simple->http);
}

const M_hash_dict_t *M_http_simple_read_headers_dict(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_headers_dict(simple->http);
}

M_list_str_t *M_http_simple_read_headers(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_headers(simple->http);
}

char *M_http_simple_read_header(const M_http_simple_read_t *simple, const char *key)
{
	if (simple == NULL)
		return NULL;
	return M_http_header(simple->http, key);
}

const M_list_str_t *M_http_simple_read_get_set_cookie(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return M_http_get_set_cookie(simple->http);
}

const unsigned char *M_http_simple_read_body(const M_http_simple_read_t *simple, size_t *len)
{
	size_t mylen;

	if (len == NULL)
		len = &mylen;
	*len = 0;

	if (simple == NULL)
		return NULL;

	*len = M_buf_len(simple->http->body);
	return (unsigned char *)M_buf_peek(simple->http->body);
}

const char *M_http_simple_read_content_type(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return simple->http->content_type;
}

const char *M_http_simple_read_origcontent_type(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return simple->http->origcontent_type;
}

M_textcodec_codec_t M_http_simple_read_codec(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return M_TEXTCODEC_UNKNOWN;
	return simple->http->codec;
}

const char *M_http_simple_read_charset(const M_http_simple_read_t *simple)
{
	if (simple == NULL)
		return NULL;
	return simple->http->charset;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

M_http_error_t M_http_simple_read(M_http_simple_read_t **simple, const unsigned char *data, size_t data_len, M_uint32 flags,
	size_t *len_read)
{
	M_http_reader_t                *reader;
	M_http_simple_read_t           *sr;
	M_http_error_t                  res;
	size_t                          mylen_read;
	M_bool                          have_simple = M_TRUE;
	struct M_http_reader_callbacks  cbs = {
		M_http_simple_read_start_cb,
		NULL, /* header_full_cb */
		M_http_simple_read_header_cb,
		M_http_simple_read_header_done_cb,
		M_http_simple_read_body_cb,
		M_http_simple_read_body_done_cb,
		M_http_simple_read_chunk_extensions_cb,
		NULL, /* chunk_extensions_done_cb */
		M_http_simple_read_chunk_data_cb,
		NULL, /* chunk_data_done_cb */
		M_http_simple_read_chunk_data_finished_cb,
		NULL, /* multipart_preamble_cb */
		NULL, /* multipart_preamble_done_cb */
		NULL, /* multipart_header_full_cb */
		NULL, /* multipart_header_cb */
		NULL, /* multipart_header_done_cb */
		NULL, /* multipart_data_cb */
		NULL, /* multipart_data_done_cb */
		NULL, /* multipart_data_finished_cb. */
		NULL, /* multipart_epilouge_cb */
		NULL, /* multipart_epilouge_done_cb */
		NULL, /* M_http_simple_read_trailer_full_cb */
		M_http_simple_read_trailer_cb,
		M_http_simple_read_trailer_done_cb
	};

	if (len_read == NULL)
		len_read = &mylen_read;
	*len_read = 0;

	if (simple == NULL) {
		have_simple = M_FALSE;
		simple      = &sr;
	}

	if (data == NULL || data_len == 0) {
		res = M_HTTP_ERROR_MOREDATA;
		goto done;
	}

	*simple = M_http_simple_read_create(flags);

	reader = M_http_reader_create(&cbs, M_HTTP_READER_NONE, *simple);
	res    = M_http_reader_read(reader, data, data_len, len_read);
	M_http_reader_destroy(reader);

	if (res == M_HTTP_ERROR_SUCCESS && !(*simple)->rdone) {
		res       = M_HTTP_ERROR_MOREDATA;
		*len_read = 0;
	}

	if (res != M_HTTP_ERROR_SUCCESS) {
		goto done;
	}

	res = M_http_simple_read_decode_body(*simple);
	if (res != M_HTTP_ERROR_SUCCESS) {
		M_http_simple_read_destroy(*simple);
		*simple = NULL;
		goto done;
	}

done:
	if (res != M_HTTP_ERROR_SUCCESS || !have_simple) {
		M_http_simple_read_destroy(*simple);
		if (have_simple) {
			*simple = NULL;
		}
	}
	return res;
}

M_http_error_t M_http_simple_read_parser(M_http_simple_read_t **simple, M_parser_t *parser, M_uint32 flags)
{
	M_http_error_t res;
	size_t         len_read = 0;

	res = M_http_simple_read(simple, M_parser_peek(parser), M_parser_len(parser), flags, &len_read);

	M_parser_consume(parser, len_read);

	return res;
}
