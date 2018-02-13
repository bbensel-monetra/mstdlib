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

#ifndef __M_IO_BLE_INT_H__
#define __M_IO_BLE_INT_H__

#include <mstdlib/io/m_io_layer.h>
#include "m_io_meta.h"

#define M_IO_BLE_NAME "BLE"

typedef struct {
	char     *name;
	char     *mac;
	char     *uuid;
	M_time_t  last_seen;
	M_bool    connected;
} M_io_ble_enum_device_t;

struct M_io_ble_enum {
	M_list_t *devices;
};

typedef struct {
	char           service_uuid[256];
	char           characteristic_uuid[256];
	unsigned char *data;
} M_io_ble_data_t;

struct M_io_handle {
	M_io_t          *io;          /*!< io object handle is associated with. */
	char             mac[256];    /*!< mac of device in use. */
	M_list_t        *read_queue;  /*!< M_io_ble_data_t */
	M_list_t        *write_queue; /*!< M_io_ble_data_t */
	M_event_timer_t *timer;       /*!< Timer to handle connection timeouts */
	M_uint64         timeout_ms;  /*!< Timeout for connecting. */
	char             error[256];  /*!< Error message. */
	M_io_state_t     state;
	M_bool           can_write;   /*!< Wether data can be written. Will be false if a write operation is processing. */
};

M_io_ble_enum_t *M_io_ble_enum_init(void);
void M_io_ble_enum_add(M_io_ble_enum_t *btenum, const char *name, const char *mac, const char *uuid, M_time_t last_seen, M_bool connected);

void M_io_ble_connect(M_io_handle_t *handle);
void M_io_ble_close(M_io_handle_t *handle);

M_list_str_t *M_io_ble_get_device_services(const char *mac);
M_list_str_t *M_io_ble_get_device_service_characteristics(const char *mac, const char *service_uuid);

M_uint64 M_io_ble_validate_timeout(M_uint64 timeout_ms);
void M_io_ble_data_destory(M_io_ble_data_t *data);
M_io_handle_t *M_io_ble_get_io_handle(M_io_t *io);

M_io_handle_t *M_io_ble_open(const char *mac, M_io_error_t *ioerr, M_uint64 timeout_ms);
M_bool M_io_ble_errormsg_cb(M_io_layer_t *layer, char *error, size_t err_len);
M_io_state_t M_io_ble_state_cb(M_io_layer_t *layer);
void M_io_ble_destroy_cb(M_io_layer_t *layer);
M_bool M_io_ble_process_cb(M_io_layer_t *layer, M_event_type_t *type);
M_io_error_t M_io_ble_write_cb(M_io_layer_t *layer, const unsigned char *buf, size_t *write_len, M_io_meta_t *meta);
M_io_error_t M_io_ble_read_cb(M_io_layer_t *layer, unsigned char *buf, size_t *read_len, M_io_meta_t *meta);
void M_io_ble_unregister_cb(M_io_layer_t *layer);
M_bool M_io_ble_disconnect_cb(M_io_layer_t *layer);
M_bool M_io_ble_init_cb(M_io_layer_t *layer);

#endif