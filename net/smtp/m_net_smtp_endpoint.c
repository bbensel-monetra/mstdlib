/* The MIT License (MIT)
 *
 * Copyright (c) 2022 Monetra Technologies, LLC.
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

#include "m_net_smtp_int.h"

M_bool M_net_smtp_endpoint_is_available(const M_net_smtp_endpoint_t *ep)
{
	return (ep->max_sessions - M_list_len(ep->send_sessions)) > 0;
}

M_bool M_net_smtp_endpoint_is_idle(const M_net_smtp_endpoint_t *ep)
{
	return (M_list_len(ep->send_sessions) == 0);
}


M_net_smtp_endpoint_t * M_net_smtp_endpoint_create_tcp(
	const char   *address,
	M_uint16      port,
	M_bool        connect_tls,
	const char   *username,
	const char   *password,
	size_t        max_conns
)
{
	M_net_smtp_endpoint_t *ep = M_malloc_zero(sizeof(*ep));

	ep->type                  = M_NET_SMTP_EPTYPE_TCP;
	ep->max_sessions          = max_conns;
	ep->send_sessions         = M_list_create(NULL, M_LIST_NONE);
	ep->idle_sessions         = M_list_create(NULL, M_LIST_NONE);
	ep->sessions_rwlock       = M_thread_rwlock_create();
	ep->tcp.address           = M_strdup(address);
	ep->tcp.username          = M_strdup(username);
	ep->tcp.password          = M_strdup(password);
	ep->tcp.port              = port;
	ep->tcp.connect_tls       = connect_tls;

	return ep;

}

M_net_smtp_endpoint_t * M_net_smtp_endpoint_create_proc(
	const char          *command,
	const M_list_str_t  *args,
	const M_hash_dict_t *env,
	M_uint64             timeout_ms,
	size_t               max_processes
)
{
	M_net_smtp_endpoint_t *ep = M_malloc_zero(sizeof(*ep));

	ep->type                  = M_NET_SMTP_EPTYPE_PROCESS;
	ep->send_sessions         = M_list_create(NULL, M_LIST_NONE);
	ep->idle_sessions         = M_list_create(NULL, M_LIST_NONE);
	ep->sessions_rwlock       = M_thread_rwlock_create();
	ep->max_sessions          = max_processes;
	ep->process.command       = M_strdup(command);
	ep->process.args          = M_list_str_duplicate(args);
	ep->process.env           = M_hash_dict_duplicate(env);
	ep->process.timeout_ms    = timeout_ms;

	return ep;
}

void M_net_smtp_endpoint_destroy(M_net_smtp_endpoint_t *ep)
{
	M_net_smtp_session_t *session;
	M_thread_rwlock_lock(ep->sessions_rwlock, M_THREAD_RWLOCK_TYPE_WRITE);
	while (
		(session = M_list_take_last(ep->send_sessions)) != NULL ||
		(session = M_list_take_last(ep->idle_sessions)) != NULL
	) {
		M_net_smtp_session_destroy(session);
	}
	M_list_destroy(ep->send_sessions, M_TRUE);
	M_list_destroy(ep->idle_sessions, M_TRUE);
	M_thread_rwlock_unlock(ep->sessions_rwlock);
	M_thread_rwlock_destroy(ep->sessions_rwlock);
	switch (ep->type) {
		case M_NET_SMTP_EPTYPE_PROCESS:
			M_free(ep->process.command);
			M_list_str_destroy(ep->process.args);
			M_hash_dict_destroy(ep->process.env);
			break;
		case M_NET_SMTP_EPTYPE_TCP:
			M_free(ep->tcp.address);
			M_free(ep->tcp.username);
			M_free(ep->tcp.password);
			break;
	}
	M_free(ep);
}
