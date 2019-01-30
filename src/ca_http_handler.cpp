#include <http_parser.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "utils/log/log.h"

#include "ca_caller_pool.h"
#include "ca_sip.h"
#include "ca_http_handler.h"

struct SResult {
	int m_iHTTPResult;
	int m_iSIPResult;
	std::string m_strCaller;
	std::string m_strCalled;
	volatile int m_iCompleted;
	SResult() : m_iHTTPResult( 0 ), m_iSIPResult( 0 ), m_iCompleted( 0 ) {}
};

static int on_url_cb( http_parser *p_psoHTTPParser, const char *at, size_t length );
static int on_message_complete_cb( http_parser *p_psoHTTPParser );
static void ca_http_handler_make_response( std::string &p_strResponse, SResult *p_psoResult );

extern CLog *g_pcoLog;

int ca_http_req_handler_cb( const struct SAcceptedSock *p_psoAcceptedSock )
{
	int iRetVal = 0;
	http_parser_settings soHTTPParserSettings;
	http_parser *psoHTTPParser;
	size_t stParsed;
	char mcBuf[32];
	ssize_t sstSockReceived;
	SResult soResult;

	http_parser_settings_init( &soHTTPParserSettings );
	soHTTPParserSettings.on_url = on_url_cb;
	soHTTPParserSettings.on_message_complete = on_message_complete_cb;

	psoHTTPParser = new http_parser;
	http_parser_init( psoHTTPParser, HTTP_REQUEST );
	psoHTTPParser->data = &soResult;

	while( 0 == soResult.m_iCompleted && 0 < ( sstSockReceived = recv( p_psoAcceptedSock->m_iAcceptedSock, mcBuf, sizeof( mcBuf ), 0 ) ) ) {
		stParsed = http_parser_execute( psoHTTPParser, &soHTTPParserSettings, mcBuf, sstSockReceived );
		if( stParsed != sstSockReceived ) {
			UTL_LOG_E( *g_pcoLog, "error: received on socket %u but operated by parser %u", sstSockReceived, stParsed );
			break;
		}
	}

	if( 0 == sstSockReceived ) {
		UTL_LOG_E( *g_pcoLog, "connection was closed unexpectedly: %s:%u", p_psoAcceptedSock->m_mcIPAddress, p_psoAcceptedSock->m_usPort );
		return ECONNRESET;
	}

	std::string strResponse;

	ca_http_handler_make_response( strResponse, &soResult );
	if( strResponse.length() == send( p_psoAcceptedSock->m_iAcceptedSock, strResponse.c_str(), strResponse.length(), 0 ) ) {
		UTL_LOG_N(
			*g_pcoLog,
			"HTTP-Response was sent successfully to %s:%u; SIP-Result-Code: %u; Caller: %s; Called: %s",
			p_psoAcceptedSock->m_mcIPAddress,
			p_psoAcceptedSock->m_usPort,
			soResult.m_iSIPResult,
			soResult.m_strCaller.c_str(),
			soResult.m_strCalled.c_str() );
	} else {
		UTL_LOG_E( *g_pcoLog, "HTTP-Response to %s:%u failed", p_psoAcceptedSock->m_mcIPAddress, p_psoAcceptedSock->m_usPort );
	}

	return iRetVal;
}

static int on_url_cb( http_parser *p_psoHTTPParser, const char *at, size_t length )
{
	int iRetVal = 0;
	int iFnRes;
	std::string strData;
	SResult *psoResult = reinterpret_cast< SResult* >( p_psoHTTPParser->data );

#ifdef DEBUG
	strData.assign( at, length );
	g_pcoLog->Dump2( "debug: ", strData.c_str() );
#endif

	http_parser_url soHTTPParserURL;

	http_parser_url_init( &soHTTPParserURL );

	if( 0 == http_parser_parse_url( at, length, 0, &soHTTPParserURL ) ) {
		UTL_LOG_D( *g_pcoLog, "http_parser_parse_url: Ok" );
		strData.assign( &at[soHTTPParserURL.field_data[UF_QUERY].off], soHTTPParserURL.field_data[UF_QUERY].len );
	#ifdef DEBUG
		g_pcoLog->Dump2( "debug: ", strData.c_str() );
	#endif
		if( 0 == strData.compare( 0, 3, "pn=" ) ) {
			const char *pszCaller;
			std::string strPhone = strData.substr( 3 );

			pszCaller = ca_caller_pool_get();
			iFnRes = ca_sip_send_req( pszCaller, strPhone.c_str() );
			if( NULL != psoResult ) {
				psoResult->m_iHTTPResult = 200;
				psoResult->m_iSIPResult = iFnRes;
				psoResult->m_strCaller = pszCaller;
				psoResult->m_strCalled = strPhone;
			}
		} else {
			if( NULL != psoResult ) {
				psoResult->m_iHTTPResult = 500;
			}
			g_pcoLog->Dump2( "error: invalid request: ", strData.c_str() );
		}
	} else {
		UTL_LOG_E( *g_pcoLog, "http_parser_parse_url: Failed" );
		if( NULL != psoResult ) {
			psoResult->m_iHTTPResult = 500;
		}
		iRetVal = -1;
	}

	return iRetVal;
}

static int on_message_complete_cb( http_parser *p_psoHTTPParser )
{
	SResult *psoResult = reinterpret_cast< SResult* >( p_psoHTTPParser->data );

	UTL_LOG_D( *g_pcoLog, "message completed" );

	if( NULL != psoResult ) {
		psoResult->m_iCompleted = 1;
	}
}

static void ca_http_handler_make_response( std::string &p_strResponse, SResult *p_psoResult )
{
	char *pszText;
	std::string strBody;

	if( 0 < asprintf( &pszText, "{ \"Caller\" : \"%s\", \"Called\" : \"%s\", \"SIP-Result-Code\" : \"%u\" }",
					  p_psoResult->m_strCaller.c_str(), p_psoResult->m_strCalled.c_str(), p_psoResult->m_iSIPResult ) ) {
		if( NULL != pszText ) {
			strBody = pszText;
			free( pszText );
		}
	}

	if( 0 < asprintf( &pszText, "%s %u %s\r\n", "HTTP/1.0", p_psoResult->m_iHTTPResult, http_status_str( static_cast< http_status >( p_psoResult->m_iHTTPResult ) ) ) ) {
		if( NULL != pszText ) {
			p_strResponse = pszText;
			free( pszText );
		}
	}
	if( 0 < asprintf( &pszText, "Content-Type: application/json\r\n" ) ) {
		if( NULL != pszText ) {
			p_strResponse += pszText;
			free( pszText );
		}
	}
	if( 0 < asprintf( &pszText, "Content-Length: %u\r\n", strBody.length() ) ) {
		if( NULL != pszText ) {
			p_strResponse += pszText;
			free( pszText );
		}
	}
	if( 0 < asprintf( &pszText, "Connection: Closed\r\n" ) ) {
		if( NULL != pszText ) {
			p_strResponse += pszText;
			free( pszText );
		}
	}
	p_strResponse += "\r\n";

	if( 0 < strBody.length() ) {
		p_strResponse += strBody;
	}
}
