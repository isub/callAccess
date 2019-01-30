#ifndef __CA_PLANNED_ACTION_H__
#define __CA_PLANNED_ACTION_H__

int ca_start_timer( void( *p_pAction )(void*), void *p_pvArg, unsigned int p_iSec, unsigned int p_iUSec );

#endif /* __CA_PLANNED_ACTION_H__ */
