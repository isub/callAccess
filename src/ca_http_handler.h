#ifndef __CA_HTTP_HANDLER_H__
#define __CA_HTTP_HANDLER_H__

#include <utils/tcp_listener/tcp_listener.h>

int ca_http_req_handler_cb( const struct SAcceptedSock *p_psoAcceptedSock );

#endif /* __CA_HTTP_HANDLER_H__ */
