#ifndef __CA_SYNC_H__
#define __CA_SYNC_H__

#include <pjlib.h>

int ca_sync_init();
void ca_sync_fini();

int  ca_sync_lock( pj_str_t *p_pstrCallId );
void ca_sync_ulck( pj_str_t *p_pstrCallId, int p_iResult, void(*p_pvAction)(void*), void *p_pvActionArg );
void ca_sync_del( pj_str_t *p_pstrCallId );

#endif /* __CA_SYNC_H__ */
