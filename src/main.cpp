#include <unistd.h>	/* sleep */
#include <pj/os.h>	/*pj_thread_sleep*/
#include <signal.h>
#include <pthread.h>

#include "utils/tcp_listener/tcp_listener.h"
#include "utils/log/log.h"

#include "ca_sip.h"
#include "ca_conf.h"
#include "ca_sync.h"
#include "ca_caller_pool.h"
#include "ca_http_handler.h"

static void on_signal( int );

extern SCAConf	*g_psoConf;
pthread_mutex_t	 g_mutexWait;
static CLog		 g_coLog;
CLog			*g_pcoLog;

int main( int argc, char *argv [] )
{
	STCPListener *psoTCPListener;
	int iErrorOnLine;

	if( 2 == argc ) {
	} else {
		return -1;
	}

	int iRetVal = 0;

	signal( SIGTERM, on_signal );
	signal( SIGINT, on_signal );

	g_pcoLog = &g_coLog;

	ca_conf_init();

	if( 0 == ( iRetVal = ca_conf_handle( argv[1] ) ) ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize configuration" );
		return iRetVal;
	}

	if( 0 == ( iRetVal = g_pcoLog->Init( g_psoConf->m_pszLogFileMask ) ) ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize logger" );
		return iRetVal;
	}

	if( 0 == ( iRetVal = ca_sync_init() ) ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize sincronizer" );
		return iRetVal;
	}

	if( 0 == ( iRetVal = ca_sip_init() ) ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize sip module" );
		return iRetVal;
	}

	if( NULL != ( psoTCPListener = tcp_listener_init(
		g_psoConf->m_pszTCPListenerAddr,
		g_psoConf->m_usTCPLIstenerPort,
		g_psoConf->m_iTCPListenerQueueLen,
		g_psoConf->m_iTCPListenerThreadCount,
		ca_http_req_handler_cb,
		&iErrorOnLine ) ) ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize tcp listener: module error code: %d", iErrorOnLine );
		return iRetVal;
	}

	if( 0 == pthread_mutex_init( &g_mutexWait, NULL ) ) {
		pthread_mutex_lock( &g_mutexWait );
	} else {
		UTL_LOG_E( *g_pcoLog, "can not initialize waiter mutex" );
	}

	pthread_mutex_lock( &g_mutexWait );

	if( NULL != psoTCPListener ) {
		tcp_listener_fini( psoTCPListener );
	}

	ca_sip_fini();

	ca_sync_fini();

	g_pcoLog->Flush();

	return 0;
}

static void on_signal( int p_iSignal )
{
	UTL_LOG_N( *g_pcoLog, "signal received: %u", p_iSignal );

	switch( p_iSignal ) {
		case SIGINT:
		case SIGTERM:
			pthread_mutex_unlock( &g_mutexWait );
			break;
	}
}
