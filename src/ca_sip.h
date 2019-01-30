#ifndef __CA_SIP_H__
#define __CA_SIP_H__

int ca_sip_init();
void ca_sip_fini();

int ca_sip_send_req( const char *p_pszCaller, const char *p_pszCalled );

#endif /* __CA_SIP_H__ */
