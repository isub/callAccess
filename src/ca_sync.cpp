#include <map>
#include <string>
#include <pthread.h>
#include <errno.h>

#include "ca.h"
#include "ca_conf.h"
#include "utils/log/log.h"
#include "ca_sync.h"

struct SSync {
	pthread_mutex_t *m_ptMutex;
	int m_iResult;
	SSync( pthread_mutex_t *p_ptMutex ) : m_ptMutex( p_ptMutex ), m_iResult( -1 ) {}
};

extern SCAConf *g_psoConf;
extern CLog *g_pcoLog;
std::map<std::string, SSync> g_mapReqLock;
static pthread_mutex_t g_mutexCASync;

int ca_sync_init()
{
	return ( pthread_mutex_init( &g_mutexCASync, NULL ) );
}

void ca_sync_fini()
{
	pthread_mutex_destroy( &g_mutexCASync );
}

int ca_sync_lock( pj_str_t *p_pstrCallId )
{
	/* проверка параметра */
	if( NULL != p_pstrCallId && NULL != p_pstrCallId->ptr && 0 < p_pstrCallId->slen ) {
	} else {
		UTL_LOG_D( *g_pcoLog, "invalid parameter" );
		return -1;
	}

	int iRetVal = -1;
	SSync soSync( new pthread_mutex_t );
	std::string strCallId;
	timespec soTS;
	std::pair<std::map<std::string, SSync>::iterator, bool> insertResult;

	/* фомируем ключ */
	strCallId.insert( 0, p_pstrCallId->ptr, p_pstrCallId->slen );

	/* инициализируем мьютекс вызова */
	pthread_mutex_init( soSync.m_ptMutex, NULL );
	/* запираем его */
	pthread_mutex_lock( soSync.m_ptMutex );

	pthread_mutex_lock( &g_mutexCASync );

	/* помещаем элемент в список */
	insertResult = g_mapReqLock.insert( std::pair<std::string, SSync>( strCallId, soSync ) );

	pthread_mutex_unlock( &g_mutexCASync );

	if( 0 == ca_make_timespec_timeout( soTS, g_psoConf->m_uiSIPTimeout, 0 ) ) {
		iRetVal = pthread_mutex_timedlock( insertResult.first->second.m_ptMutex, &soTS );
		if( 0 == iRetVal ) {
			UTL_LOG_D( *g_pcoLog, "mutex unlocked: Call-Id: %s; Result-Code: %u", strCallId.c_str(), insertResult.first->second.m_iResult );
			iRetVal = insertResult.first->second.m_iResult;
		} else {
			iRetVal = -ETIMEDOUT;
			UTL_LOG_D( *g_pcoLog, "timed out: Call-Id: %s", strCallId.c_str() );
		}
	} else {
		iRetVal = -1;
	}

	return iRetVal;
}

void ca_sync_ulck( pj_str_t *p_pstrCallId, int p_iResult, void( *p_pvAction )( void* ), void *p_pvActionArg )
{
	if( NULL != p_pstrCallId && NULL != p_pstrCallId->ptr && 0 < p_pstrCallId->slen ) {
	} else {
		UTL_LOG_D( *g_pcoLog, "invalid parameter" );
		return;
	}

	std::string strCallId;
	std::map<std::string, SSync>::iterator iter;

	strCallId.insert( 0, p_pstrCallId->ptr, p_pstrCallId->slen );

	pthread_mutex_lock( &g_mutexCASync );

	iter = g_mapReqLock.find( strCallId );
	if( iter != g_mapReqLock.end() ) {
		if( NULL != p_pvAction ) {
			p_pvAction( p_pvActionArg );
		}
		iter->second.m_iResult = p_iResult;
		pthread_mutex_unlock( iter->second.m_ptMutex );
		UTL_LOG_D( *g_pcoLog, "syncroniser for %s is unlocked", strCallId.c_str() );
	} else {
		UTL_LOG_D( *g_pcoLog, "syncroniser for %s not found", strCallId.c_str() );
	}

	pthread_mutex_unlock( &g_mutexCASync );
}

void ca_sync_del( pj_str_t *p_pstrCallId )
{
	if( NULL != p_pstrCallId && NULL != p_pstrCallId->ptr && 0 < p_pstrCallId->slen ) {
	} else {
		UTL_LOG_D( *g_pcoLog, "invalid parameter" );
		return;
	}

	std::string strCallId;
	std::map<std::string, SSync>::iterator iter;

	strCallId.insert( 0, p_pstrCallId->ptr, p_pstrCallId->slen );

	pthread_mutex_lock( &g_mutexCASync );

	iter = g_mapReqLock.find( strCallId );
	if( iter != g_mapReqLock.end() ) {
		pthread_mutex_destroy( iter->second.m_ptMutex );
		delete iter->second.m_ptMutex;
		g_mapReqLock.erase( iter );
		UTL_LOG_D( *g_pcoLog, "syncroniser for %s is destroyed", strCallId.c_str() );
	} else {
		UTL_LOG_D( *g_pcoLog, "syncroniser for %s not found", strCallId.c_str() );
	}

	pthread_mutex_unlock( &g_mutexCASync );
}
