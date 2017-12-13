/* The MIT License (MIT)
 * 
 * Copyright (c) 2017 Main Street Softworks, Inc.
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
#include <mstdlib/io/m_io_bluetooth.h>
#include "m_io_bluetooth_int.h"

static void M_io_bluetooth_enum_free_device(void *arg)
{
	M_io_bluetooth_enum_device_t *device = arg;
	M_free(device->name);
	M_free(device->mac);
	M_list_str_destroy(device->service_names);
	M_list_str_destroy(device->service_uuids);
	M_free(device);
}

void M_io_bluetooth_enum_destroy(M_io_bluetooth_enum_t *btenum)
{
	if (btenum == NULL)
		return;
	M_list_destroy(btenum->devices, M_TRUE);
	M_free(btenum);
}

M_io_bluetooth_enum_t *M_io_bluetooth_enum_init(void)
{
	M_io_bluetooth_enum_t  *btenum  = M_malloc_zero(sizeof(*btenum));
	struct M_list_callbacks listcbs = {
		NULL,
		NULL,
		NULL,
		M_io_bluetooth_enum_free_device
	};
	btenum->devices = M_list_create(&listcbs, M_LIST_NONE);
	return btenum;
}

size_t M_io_bluetooth_enum_count(const M_io_bluetooth_enum_t *btenum)
{
	if (btenum == NULL)
		return 0;
	return M_list_len(btenum->devices);
}


void M_io_bluetooth_enum_add(M_io_bluetooth_enum_t *btenum, const char *name, const char *mac, const M_list_str_t *service_names, const M_list_str_t *service_uuids)
{
	M_io_bluetooth_enum_device_t *device;
	M_list_str_t                 *tlist;
	size_t                        len;
	size_t                        i;

	if (btenum == NULL || M_str_isempty(name) || M_str_isempty(mac) || M_list_str_len(service_uuids) == 0 || (service_names != NULL && M_list_str_len(service_names) != M_list_str_len(service_uuids)))
		return;

	device       = M_malloc_zero(sizeof(*device));
	device->name = M_strdup(name);
	device->mac  = M_strdup(mac);

	if (service_names == NULL) {
		tlist = M_list_str_create(M_LIST_STR_NONE);
		len   = M_list_str_len(service_uuids);
		for (i=0; i<len; i++) {
			M_list_str_insert(tlist, "");
		}
	} else {
		tlist = M_list_str_duplicate(service_names);
	}
	device->service_names = tlist;
	device->service_uuids = M_list_str_duplicate(service_uuids);

	M_list_insert(btenum->devices, device);
}

const char *M_io_bluetooth_enum_name(const M_io_bluetooth_enum_t *btenum, size_t idx)
{
	const M_io_bluetooth_enum_device_t *device;
	if (btenum == NULL)
		return NULL;
	device = M_list_at(btenum->devices, idx);
	if (device == NULL)
		return NULL;
	return device->name;
}

const char *M_io_bluetooth_enum_mac(const M_io_bluetooth_enum_t *btenum, size_t idx)
{
	const M_io_bluetooth_enum_device_t *device;
	if (btenum == NULL)
		return NULL;
	device = M_list_at(btenum->devices, idx);
	if (device == NULL)
		return NULL;
	return device->mac;
}

M_bool M_io_bluetooth_enum_service_id(const M_io_bluetooth_enum_t *btenum, size_t idx, size_t sidx, const char **name, const char **uuid)
{
	const M_io_bluetooth_enum_device_t *device;

	if (btenum == NULL)
		return M_FALSE;

	device = M_list_at(btenum->devices, idx);
	if (device == NULL)
		return M_FALSE;

	if (name != NULL)
		*name = M_list_str_at(device->service_names, sidx);
	if (uuid != NULL)
		*uuid = M_list_str_at(device->service_uuids, sidx);

	return M_TRUE;

}

M_io_error_t M_io_bluetooth_create(M_io_t **io_out, const char *mac, const char *uuid)
{
	M_io_handle_t    *handle;
	M_io_callbacks_t *callbacks;
	M_io_error_t      err;

	if (io_out == NULL || M_str_isempty(mac))
		return M_IO_ERROR_INVALID;

	handle    = M_io_bluetooth_open(mac, uuid, &err);
	if (handle == NULL)
		return err;

	*io_out   = M_io_init(M_IO_TYPE_STREAM);
	callbacks = M_io_callbacks_create();
	M_io_callbacks_reg_init(callbacks, M_io_bluetooth_init_cb);
	M_io_callbacks_reg_read(callbacks, M_io_bluetooth_read_cb);
	M_io_callbacks_reg_write(callbacks, M_io_bluetooth_write_cb);
	M_io_callbacks_reg_processevent(callbacks, M_io_bluetooth_process_cb);
	M_io_callbacks_reg_unregister(callbacks, M_io_bluetooth_unregister_cb);
	M_io_callbacks_reg_destroy(callbacks, M_io_bluetooth_destroy_cb);
	M_io_callbacks_reg_disconnect(callbacks, M_io_bluetooth_disconnect_cb);
	M_io_callbacks_reg_state(callbacks, M_io_bluetooth_state_cb);
	M_io_callbacks_reg_errormsg(callbacks, M_io_bluetooth_errormsg_cb);
	M_io_layer_add(*io_out, M_IO_BLUETOOTH_NAME, handle, callbacks);
	M_io_callbacks_destroy(callbacks);

	return M_IO_ERROR_SUCCESS;
}

