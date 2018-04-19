/* The MIT License (MIT)
 * 
 * Copyright (c) 2018 Main Street Softworks, Inc.
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

void M_http_set_headers_int(M_hash_dict_t **cur_headers, const M_hash_dict_t *new_headers, M_bool merge)
{
	M_list_str_t       *l;
	M_hash_dict_enum_t *he;
	const char         *key;
	size_t              len;
	size_t              i;

	if (new_headers == NULL && merge)
		return;

	if (!merge) {
		M_hash_dict_destroy(*cur_headers);
		*cur_headers = M_hash_dict_create(8, 75, M_HASH_DICT_CASECMP|M_HASH_DICT_KEYS_ORDERED|M_HASH_DICT_MULTI_VALUE|M_HASH_DICT_MULTI_CASECMP);
		M_hash_dict_merge(cur_headers, M_hash_dict_duplicate(new_headers));
		return;
	}

	/* We're going to iterate over every item in new header for each
 	 * key. We'll do the same for the current headers and push them
	 * all into a set to remove duplicates. Then we'll put them all
	 * back into the headers. */
	M_hash_dict_enumerate(new_headers, &he);
	while (M_hash_dict_enumerate_next(new_headers, he, &key, NULL)) {
		if (M_hash_dict_multi_len(new_headers, key, &len) && len > 0) {
			/* keep unsorted because we want all header values in
 			 * the order they were set. */
			l = M_list_str_create(M_LIST_STR_CASECMP|M_LIST_STR_SET);

			len = 0;
			M_hash_dict_multi_len(*cur_headers, key, &len);
			for (i=0; i<len; i++) {
				M_list_str_insert(l, M_hash_dict_multi_get_direct(*cur_headers, key, i));
			}

			len = 0;
			M_hash_dict_multi_len(new_headers, key, &len);
			for (i=0; i<len; i++) {
				M_list_str_insert(l, M_hash_dict_multi_get_direct(new_headers, key, i));
			}

			M_hash_dict_remove(*cur_headers, key);
			len = M_list_str_len(l);
			for (i=0; i<len; i++) {
				M_hash_dict_insert(*cur_headers, key, M_list_str_at(l, i));
			}

			M_list_str_destroy(l);
		}
	}
	M_hash_dict_enumerate_free(he);
}

char *M_http_header_int(const M_hash_dict_t *d, const char *key)
{
	M_list_str_t *l;
	char         *out;
	size_t        len;
	size_t        i;

	if (!M_hash_dict_multi_len(d, key, &len) || len == 0)
		return NULL;

	if (len == 1)
		return M_strdup(M_hash_dict_multi_get_direct(d, key, 0));

	l = M_list_str_create(M_LIST_STR_NONE);
	for (i=0; i<len; i++) {
		M_list_str_insert(l, M_hash_dict_multi_get_direct(d, key, i));
	}

	out = M_list_str_join_str(l, ", ");
	M_list_str_destroy(l);
	return out;
}
