#include <pthread.h>
#include <errno.h>
#include <pj/os.h>

#include "utils/log/log.h"
#include "ca.h"
#include "ca_planned_action.h"

extern CLog *g_pcoLog;

struct SPlanedActionData {
	void( *m_pAction )( void* );
	void *m_pvArg;
	unsigned int m_uiSec;
	unsigned int m_uiUSec;
	SPlanedActionData( void( *p_pAction )( void* ), void *p_pvArg, unsigned int p_uiSec, unsigned int p_uiUSec ) :
		m_pAction( p_pAction ), m_pvArg( p_pvArg ), m_uiSec( p_uiSec ), m_uiUSec( p_uiUSec ) {}
};

static void * ca_pa_thread( void* );

int ca_start_timer( void( *p_pAction )( void* ), void *p_pvArg, unsigned int p_uiSec, unsigned int p_uiUSec )
{
	int iRetVal = 0;
	SPlanedActionData *psoArg = new SPlanedActionData( p_pAction, p_pvArg, p_uiSec, p_uiUSec );
	pthread_t threadPlanedAction;

	if( 0 == ( iRetVal == pthread_create( &threadPlanedAction, NULL, ca_pa_thread, psoArg ) ) ) {
		pthread_detach( threadPlanedAction );
		UTL_LOG_D( *g_pcoLog, "waiter started successfully" );
	} else {
		UTL_LOG_D( *g_pcoLog, "waiter started successfully" );
	}

	return iRetVal;
}

static void * ca_pa_thread( void * p_pvArg )
{
	SPlanedActionData *psoArg = reinterpret_cast< SPlanedActionData * >( p_pvArg );
	pthread_mutex_t mutexWait;
	timespec soTimeSpec;

	if( 0 == pthread_mutex_init( &mutexWait, NULL ) ) {
		UTL_LOG_D( *g_pcoLog, "mutex was initialized successfully" );
	} else {
		goto __free_allocated_objects__;
	}
	if( 0 == pthread_mutex_lock( &mutexWait ) ) {
		UTL_LOG_D( *g_pcoLog, "put mutex into locked state" );
	} else {
		goto __exit_and_cleanup__;
	}

	if( 0 == ca_make_timespec_timeout( soTimeSpec, psoArg->m_uiSec, psoArg->m_uiUSec ) ) {
		UTL_LOG_D( *g_pcoLog, "prepared wait time interval: %u sec; %u usec", psoArg->m_uiSec, psoArg->m_uiUSec );
	} else {
		goto __exit_and_cleanup__;
	}

	if( ETIMEDOUT == pthread_mutex_timedlock( &mutexWait, &soTimeSpec ) ) {
		psoArg->m_pAction( psoArg->m_pvArg );
		UTL_LOG_D( *g_pcoLog, "planned action was executed" );
	}

__exit_and_cleanup__:
	pthread_mutex_destroy( &mutexWait );

__free_allocated_objects__:
	if( NULL != psoArg ) {
		delete psoArg;
	}

	pthread_exit( NULL );
}
