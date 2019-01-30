#include <stdlib.h>
#include <string.h>
#include <vector>
#include <time.h>

#include "ca_caller_pool.h"

static bool bIsInitialized;
static random_data g_soRandomData;
static char g_mcRandState[256];
static std::vector<const char *> g_vectorColler;

void ca_caller_pool_add( const char *p_pszPhoneNumber )
{
	const char *pszChar = strdup( p_pszPhoneNumber );
	g_vectorColler.push_back( pszChar );

	if( bIsInitialized ) {
	} else {
		memset( g_mcRandState, 0, sizeof( g_mcRandState ) );
		memset( &g_soRandomData, 0, sizeof( g_soRandomData ) );
		initstate_r( time( NULL ), g_mcRandState, sizeof( g_mcRandState ), &g_soRandomData );
		setstate_r( g_mcRandState, &g_soRandomData );

		bIsInitialized = true;
	}
}

const char * ca_caller_pool_get()
{
	const char * pszRetVal;
	int32_t i32Random;

	random_r( &g_soRandomData, &i32Random );

	pszRetVal = g_vectorColler[i32Random % g_vectorColler.size()];

	return pszRetVal;
}
