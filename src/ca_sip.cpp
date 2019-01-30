#include <errno.h>
#include <string>
#include <stdio.h>
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <sys/time.h>

#include "ca.h"
#include "ca_conf.h"
#include "utils/log/log.h"
#include "ca_sync.h"
#include "ca_planned_action.h"
#include "ca_sip.h"

struct SActionData {
	pjsip_dialog	*m_psoDialog;
	int				 m_iStatus;
	pj_int32_t		 m_i32CSec;
	pj_bool_t		 m_bIsNeedRegisterThread;
	SActionData( pjsip_dialog *p_psoDialog, int p_iStatus, pj_int32_t p_i32CSec, pj_bool_t p_bRegister = PJ_FALSE ) :
		m_psoDialog( p_psoDialog ), m_iStatus( p_iStatus ), m_i32CSec( p_i32CSec ), m_bIsNeedRegisterThread( p_bRegister ) {}
};

extern SCAConf *g_psoConf;

extern CLog *g_pcoLog;

static pj_caching_pool     g_soCachPool;
static pjsip_endpoint     *g_psoEndPoint;
static pj_pool_t          *g_ptPool;
static pjsip_transport    *g_psoTrans;
static pjsip_auth_clt_sess g_soAuthSess;
static pj_bool_t           g_bQuitFlag;
static pjsip_user_agent   *g_psoUserAgent;
static pj_thread_t        *g_threadHandler;

static int worker_thread( void *arg );
static pj_bool_t on_rx_request( pjsip_rx_data *p_psoRxData );
static pj_bool_t on_rx_response( pjsip_rx_data *p_psoRxData );
static pj_bool_t on_tx_request( pjsip_tx_data *p_psoTxData );
static pj_bool_t on_tx_response( pjsip_tx_data *p_psoTxData );
static void on_tsx_state( pjsip_transaction *p_pTrans, pjsip_event *p_pEvent );
static int init_auth();
static void sip_make_sdp( pj_str_t *p_strSDP );
static void sip_make_body( pj_str_t *p_strBody );
static void send_request( pjsip_dialog *p_psoDialog, const pjsip_method *p_pMethod );
static void send_ack( pjsip_dialog *p_psoDialog, pjsip_rx_data *p_psoRxData );
static void send_bye( SActionData *p_psoActionData );
static void send_resp( pjsip_dialog *p_psoDialog, pjsip_rx_data *p_psoRxData, int p_iStatus, const char *p_pszStatus );
static void ca_hang_up( void *p_pvArg );

static pjsip_module g_soModApp = {
  NULL, NULL,                            /* prev, next.      */
  { const_cast< char* >( "mod-app" ), 7 }, /* Name.            */
  -1,                                    /* Id               */
  PJSIP_MOD_PRIORITY_APPLICATION,        /* Priority         */
  NULL,                                  /* load()           */
  NULL,                                  /* start()          */
  NULL,                                  /* stop()           */
  NULL,                                  /* unload()         */
  &on_rx_request,                        /* on_rx_request()  */
  &on_rx_response,                       /* on_rx_response() */
  &on_tx_request,                        /* on_tx_request()  */
  &on_tx_response,                       /* on_tx_response() */
  &on_tsx_state                          /* on_tsx_state()   */
};

int ca_sip_init()
{
	int iRetVal = 0;

	pj_status_t tStatus;

	/* инициализация библиотеки */
	tStatus = pj_init();
	PJ_ASSERT_RETURN( tStatus == PJ_SUCCESS, 1 );

	/* инициализация утилит */
	tStatus = pjlib_util_init();
	PJ_ASSERT_RETURN( tStatus == PJ_SUCCESS, 1 );

	/* Must create a pool factory before we can allocate any memory. */
	pj_caching_pool_init( &g_soCachPool, &pj_pool_factory_default_policy, 0 );

	/* Create the endpoint: */
	tStatus = pjsip_endpt_create( &g_soCachPool.factory, "sipstateless", &g_psoEndPoint );
	PJ_ASSERT_RETURN( tStatus == PJ_SUCCESS, 1 );

	{
		pj_sockaddr_in soAddr;
		pj_str_t strSIPLocalAddr;

		strSIPLocalAddr = pj_str( const_cast< char* >( g_psoConf->m_pszSIPLocalAddr ) );
		pj_inet_aton( &strSIPLocalAddr, &soAddr.sin_addr );

		soAddr.sin_family = pj_AF_INET();
		if( 0 != pj_inet_aton( &strSIPLocalAddr, &soAddr.sin_addr ) ) {
		} else {
			return -1;
		}
		soAddr.sin_port = pj_htons( g_psoConf->m_usSIPLocalPort );

		tStatus = pjsip_udp_transport_start( g_psoEndPoint, &soAddr, NULL, 1, &g_psoTrans );
		if( tStatus != PJ_SUCCESS ) {
			PJ_LOG( 3, ( __FILE__, "Error starting UDP transport (port in use?)" ) );
			return 1;
		}
	}

	tStatus = pjsip_tsx_layer_init_module( g_psoEndPoint );
	pj_assert( tStatus == PJ_SUCCESS );

	tStatus = pjsip_ua_init_module( g_psoEndPoint, NULL );
	pj_assert( tStatus == PJ_SUCCESS );

	/*
	 * Register our module to receive incoming requests.
	 */
	tStatus = pjsip_endpt_register_module( g_psoEndPoint, &g_soModApp );
	PJ_ASSERT_RETURN( tStatus == PJ_SUCCESS, 1 );

	g_ptPool = pjsip_endpt_create_pool( g_psoEndPoint, "", 4096, 4096 );

	tStatus = pj_thread_create( g_ptPool, "", &worker_thread, NULL, 0, 0, &g_threadHandler );
	PJ_ASSERT_RETURN( tStatus == PJ_SUCCESS, 1 );

	g_psoUserAgent = pjsip_ua_instance();

	iRetVal = init_auth();

	return iRetVal;
}

void ca_sip_fini()
{
	g_bQuitFlag = true;
	pj_thread_join( g_threadHandler );
}

/* Worker thread */
static int worker_thread( void *arg )
{
	PJ_UNUSED_ARG( arg );

	while( ! g_bQuitFlag ) {
		pj_time_val soTimeOut = { 0, 500 };
		pjsip_endpt_handle_events( g_psoEndPoint, &soTimeOut );
	}

	return 0;
}

static pj_bool_t on_rx_request( pjsip_rx_data *p_psoRxData )
{
	pj_bool_t bRetVal = PJ_FALSE;
	pjsip_dialog *psoDialog;
	pjsip_method_e eSIPMethod;
	int iStatusCode;

	psoDialog = pjsip_rdata_get_dlg( p_psoRxData );
	if( NULL != psoDialog ) {
		pjsip_transaction *psoTrans = pjsip_rdata_get_tsx( p_psoRxData );

		if( NULL != psoTrans ) {
			eSIPMethod = psoTrans->method.id;
			iStatusCode = psoTrans->status_code;
		} else {
			eSIPMethod = p_psoRxData->msg_info.cseq->method.id;
			iStatusCode = p_psoRxData->msg_info.msg->line.status.code;
		}
		switch( eSIPMethod ) {
			case PJSIP_INVITE_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_INVITE_METHOD" );
				break;
			case PJSIP_CANCEL_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_CANCEL_METHOD" );
				break;
			case PJSIP_ACK_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_ACK_METHOD" );
				break;
			case PJSIP_BYE_METHOD:
				send_resp( psoDialog, p_psoRxData, 200, NULL );
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_BYE_METHOD" );
				break;
			case PJSIP_REGISTER_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_REGISTER_METHOD" );
				break;
			case PJSIP_OPTIONS_METHOD:
				send_resp( psoDialog, p_psoRxData, 200, NULL );
				bRetVal = PJ_TRUE;
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OPTIONS_METHOD" );
				break;
			case PJSIP_OTHER_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OTHER_METHOD" );
				break;
			default:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: %d", eSIPMethod );
		}
	} else {
	  /* non Dialog message */
		bRetVal = PJ_FALSE;
	}

	return bRetVal;
}

static pj_bool_t on_rx_response( pjsip_rx_data *p_psoRxData )
{
	pj_bool_t bRetVal = PJ_FALSE;
	pjsip_dialog *psoDialog;
	pjsip_method_e eSIPMethod;
	int iStatusCode;

	psoDialog = pjsip_rdata_get_dlg( p_psoRxData );
	if( NULL != psoDialog ) {
		pjsip_transaction *psoTrans = pjsip_rdata_get_tsx( p_psoRxData );

		if( NULL != psoTrans ) {
			eSIPMethod = psoTrans->method.id;
			iStatusCode = psoTrans->status_code;
		} else {
			eSIPMethod = p_psoRxData->msg_info.cseq->method.id;
			iStatusCode = p_psoRxData->msg_info.msg->line.status.code;
		}

		switch( eSIPMethod ) {
			case PJSIP_INVITE_METHOD:
				switch( iStatusCode ) {
					case PJSIP_SC_UNAUTHORIZED:
					case PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED:
						break;
					case PJSIP_SC_BUSY_HERE:
						send_ack( psoDialog, p_psoRxData );
						ca_sync_ulck( &psoDialog->call_id->id, iStatusCode, NULL, NULL );
						break;
					case PJSIP_SC_OK:
						send_ack( psoDialog, p_psoRxData );
						{
							SActionData *psoActionData = new SActionData( psoDialog, iStatusCode, p_psoRxData->msg_info.cseq->cseq );
							send_bye( psoActionData );
						}
						bRetVal = PJ_TRUE;
						ca_sync_ulck( &psoDialog->call_id->id, iStatusCode, NULL, NULL );
					case PJSIP_SC_RINGING:
						bRetVal = PJ_TRUE;
						{
							SActionData *psoActionData = new SActionData( psoDialog, iStatusCode, p_psoRxData->msg_info.cseq->cseq, PJ_TRUE );
							ca_start_timer( ca_hang_up, psoActionData, g_psoConf->m_uiRingingSec, g_psoConf->m_uiRingingUSec );
						}
						break;
					default:
						if( 300 <= iStatusCode ) {
							send_ack( psoDialog, p_psoRxData );
							ca_sync_ulck( &psoDialog->call_id->id, iStatusCode, NULL, NULL );
							pjsip_dlg_dec_session( psoDialog, &g_soModApp );
							bRetVal = PJ_TRUE;
						}
						break;
				}
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_INVITE_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_CANCEL_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_CANCEL_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_ACK_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_ACK_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_BYE_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_BYE_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_REGISTER_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_REGISTER_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_OPTIONS_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OPTIONS_METHOD: status: %d", iStatusCode );
				break;
			case PJSIP_OTHER_METHOD:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OTHER_METHOD: status: %d", iStatusCode );
				break;
			default:
				UTL_LOG_D( *g_pcoLog, "SIP METHOD: %d: status: %d", eSIPMethod, iStatusCode );
		}
	} else {
	  /* non Dialog message */
		bRetVal = PJ_FALSE;
	}

	return bRetVal;
}

static pj_bool_t on_tx_request( pjsip_tx_data *p_psoTxData )
{
	pj_bool_t bRetVal = PJ_FALSE;
	pjsip_method_e eSIPMethod;

	eSIPMethod = p_psoTxData->msg->line.req.method.id;
	switch( eSIPMethod ) {
		case PJSIP_INVITE_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_INVITE_METHOD" );
			break;
		case PJSIP_CANCEL_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_CANCEL_METHOD" );
			break;
		case PJSIP_ACK_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_ACK_METHOD" );
			break;
		case PJSIP_BYE_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_BYE_METHOD" );
			break;
		case PJSIP_REGISTER_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_REGISTER_METHOD" );
			break;
		case PJSIP_OPTIONS_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OPTIONS_METHOD" );
			break;
		case PJSIP_OTHER_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OTHER_METHOD" );
			break;
		default:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: %d", eSIPMethod );
			break;
	}

	return bRetVal;
}

static pj_bool_t on_tx_response( pjsip_tx_data *p_psoTxData )
{
	pj_bool_t bRetVal = PJ_FALSE;
	pjsip_method_e eSIPMethod;
	int iStatusCode;

	eSIPMethod = p_psoTxData->msg->line.req.method.id;
	iStatusCode = p_psoTxData->msg->line.status.code;
	switch( eSIPMethod ) {
		case PJSIP_INVITE_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_INVITE_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_CANCEL_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_CANCEL_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_ACK_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_ACK_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_BYE_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_BYE_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_REGISTER_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_REGISTER_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_OPTIONS_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OPTIONS_METHOD: status: %d", iStatusCode );
			break;
		case PJSIP_OTHER_METHOD:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: PJSIP_OTHER_METHOD: status: %d", iStatusCode );
			break;
		default:
			UTL_LOG_D( *g_pcoLog, "SIP METHOD: %d: status: %d", eSIPMethod, iStatusCode );
	}

	return bRetVal;
}

static void on_tsx_state( pjsip_transaction *p_pTrans, pjsip_event *p_pEvent )
{
	pj_assert( p_pEvent->type == PJSIP_EVENT_TSX_STATE );
	pj_status_t tStatus;

	int iCode = p_pEvent->body.tsx_state.tsx->status_code;

	{
		const char *pszState;
		switch( p_pTrans->state ) {
			case PJSIP_TSX_STATE_NULL:
				pszState = "PJSIP_TSX_STATE_NULL";
				break;
			case PJSIP_TSX_STATE_CALLING:
				pszState = "PJSIP_TSX_STATE_CALLING";
				break;
			case PJSIP_TSX_STATE_TRYING:
				pszState = "PJSIP_TSX_STATE_TRYING";
				break;
			case PJSIP_TSX_STATE_PROCEEDING:
				pszState = "PJSIP_TSX_STATE_PROCEEDING";
				break;
			case PJSIP_TSX_STATE_COMPLETED:
				pszState = "PJSIP_TSX_STATE_COMPLETED";
				break;
			case PJSIP_TSX_STATE_CONFIRMED:
				pszState = "PJSIP_TSX_STATE_CONFIRMED";
				break;
			case PJSIP_TSX_STATE_TERMINATED:
				pszState = "PJSIP_TSX_STATE_TERMINATED";
				break;
			case PJSIP_TSX_STATE_DESTROYED:
				pszState = "PJSIP_TSX_STATE_DESTROYED";
				break;
			case PJSIP_TSX_STATE_MAX:
				pszState = "PJSIP_TSX_STATE_MAX";
				break;
			default:
				pszState = "PJSIP_TSX_STATE_UNDEFINED";
				break;
		}
		UTL_LOG_D( *g_pcoLog, "transaction state: code: '%d'; name: '%s'; event code: '%d'", p_pTrans->state, pszState, iCode );
	}

	if( PJSIP_TSX_STATE_TERMINATED == p_pTrans->state ) {
	} else if( 401 == iCode || 407 == iCode ) {
		pjsip_tx_data *new_request;
		tStatus = pjsip_auth_clt_reinit_req( &g_soAuthSess, p_pEvent->body.tsx_state.src.rdata, p_pEvent->body.tsx_state.tsx->last_tx, &new_request );

		if( tStatus == PJ_SUCCESS ) {
			tStatus = pjsip_endpt_send_request( g_psoEndPoint, new_request, -1, NULL, NULL );
		} else {
			UTL_LOG_E( *g_pcoLog, "Authentication failed!!!" );
		}
	}
}

static int init_auth()
{
	pj_status_t tStatus;

	tStatus = pjsip_auth_clt_init( &g_soAuthSess, g_psoEndPoint, g_ptPool, 0 );
	pj_assert( tStatus == PJ_SUCCESS );

	pjsip_cred_info soCredInfo;
	memset( &soCredInfo, 0, sizeof( soCredInfo ) );
	soCredInfo.realm = pj_str( const_cast< char* >( g_psoConf->m_pszSIPAuthRealm ) );
	soCredInfo.scheme = pj_str( const_cast< char* >( g_psoConf->m_pszSIPAuthScheme ) );
	soCredInfo.username = pj_str( const_cast< char* >( g_psoConf->m_pszSIPAuthUserName ) );
	soCredInfo.data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	soCredInfo.data = pj_str( const_cast< char* >( g_psoConf->m_pszSIPAuthPassword ) );
	tStatus = pjsip_auth_clt_set_credentials( &g_soAuthSess, 1, &soCredInfo );
	pj_assert( tStatus == PJ_SUCCESS );

	return 0;
}

int ca_sip_send_req( const char *p_pszCaller, const char *p_pszCalled )
{
	int iRetVal = 0;
	int iFnRes;
	char *pszLocal = NULL;
	pj_str_t strLocal;
	char *pszRemote = NULL;
	pj_str_t strRemote;
	pj_status_t tStatus;
	pj_thread_desc tDesc;
	pj_thread_t *threadThis;

	pj_bzero( tDesc, sizeof( tDesc ) );

	tStatus = pj_thread_register( __FILE__, tDesc, &threadThis );
	pj_assert( tStatus == PJ_SUCCESS );

	do {
		if( g_psoConf->m_usSIPSrvPort == 5060 || g_psoConf->m_usSIPSrvPort == 0 ) {
			iFnRes = asprintf( &pszLocal, "<sip:%s@%s>", p_pszCaller, g_psoConf->m_pszSIPSrvAdr );
		} else {
			iFnRes = asprintf( &pszLocal, "<sip:%s@%s:%u>", p_pszCaller, g_psoConf->m_pszSIPSrvAdr, g_psoConf->m_usSIPSrvPort );
		}
		if( -1 != iFnRes && NULL != pszLocal ) {
			pj_strset2( &strLocal, pszLocal );
		} else {
			iRetVal = -1;
			break;
		}

		if( g_psoConf->m_usSIPSrvPort == 5060 || g_psoConf->m_usSIPSrvPort == 0 ) {
			iFnRes = asprintf( &pszRemote, "<sip:%s@%s>", p_pszCalled, g_psoConf->m_pszSIPSrvAdr );
		} else {
			iFnRes = asprintf( &pszRemote, "<sip:%s@%s:%u>", p_pszCalled, g_psoConf->m_pszSIPSrvAdr, g_psoConf->m_usSIPSrvPort );
		}
		if( -1 != iFnRes && NULL != pszRemote ) {
			pj_strset2( &strRemote, pszRemote );
		} else {
			iRetVal = -1;
			break;
		}

		pjsip_dialog *psoDialog = NULL;

		tStatus = pjsip_dlg_create_uac( g_psoUserAgent, &strLocal, NULL, &strRemote, NULL, &psoDialog );
		pj_assert( tStatus == PJ_SUCCESS );

		pjsip_dlg_inc_lock( psoDialog );

		pj_str_t *psoCallId = &psoDialog->call_id->id;

		tStatus = pjsip_dlg_add_usage( psoDialog, &g_soModApp, NULL );
		pj_assert( tStatus == PJ_SUCCESS );

		pjsip_dlg_inc_session( psoDialog, &g_soModApp );

		send_request( psoDialog, &pjsip_invite_method );
		pjsip_dlg_dec_lock( psoDialog );

		iRetVal = ca_sync_lock( psoCallId );
		ca_sync_del( psoCallId );
	} while( 0 );

	if( NULL != pszLocal ) {
		free( pszLocal );
	}
	if( NULL != pszRemote ) {
		free( pszRemote );
	}

	return iRetVal;
}

static void sip_make_sdp( pj_str_t *p_strSDP )
{
	p_strSDP->ptr = reinterpret_cast< char* >( pj_pool_alloc( g_ptPool, 4096 ) );

	/* версия SDP */
	pj_strcat2( p_strSDP, "v: 0\r\n" );
	/* */
	pj_strcat2( p_strSDP, "o=callAccess 0 0 IN IP4 0.0.0.0\r\n" );
	pj_strcat2( p_strSDP, "s=callAccess\r\n" );
	pj_strcat2( p_strSDP, "c=IN IP4 0.0.0.0\r\n" );
	pj_strcat2( p_strSDP, "t=0 0\r\n" );
	pj_strcat2( p_strSDP, "m=audio 5060 RTP/AVP 8\r\n" );
	pj_strcat2( p_strSDP, "a=inactive\r\n" );
}

static void sip_make_body( pj_str_t *p_strBody )
{
	pj_str_t soSDP = pj_str( NULL );

	sip_make_sdp( &soSDP );

	pj_strcat( p_strBody, &soSDP );
}

/* Send request */
static void send_request( pjsip_dialog *p_psoDialog, const pjsip_method *p_pMethod )
{
	pjsip_tx_data     *pTData;
	pj_str_t strBody = pj_str( NULL );
	pj_status_t        tStatus;
	pj_thread_t       *threadThis;

	tStatus = pjsip_dlg_create_request( p_psoDialog, p_pMethod, -1, &pTData );
	pj_assert( tStatus == PJ_SUCCESS );

	/* формируем тело запроса */
	char mcBody[4096] = { '\0' };
	pj_strset2( &strBody, mcBody );
	sip_make_body( &strBody );

	pjsip_msg_body *pBody;
	pj_str_t strContentType = pj_str( const_cast< char* >( "application" ) );
	pj_str_t strContSubType = pj_str( const_cast< char* >( "sdp" ) );

	strBody.slen = pj_ansi_strlen( strBody.ptr );
	pBody = pjsip_msg_body_create( pTData->pool, &strContentType, &strContSubType, &strBody );
	pTData->msg->body = pBody;

	tStatus = pjsip_dlg_send_request( p_psoDialog, pTData, -1, NULL );
	pj_assert( tStatus == PJ_SUCCESS );
}

static void send_ack( pjsip_dialog *p_psoDialog, pjsip_rx_data *p_psoRxData )
{
	pjsip_tx_data *psoTxData;
	pj_status_t tStatus;

	tStatus = pjsip_dlg_create_request( p_psoDialog, &pjsip_ack_method, p_psoRxData->msg_info.cseq->cseq, &psoTxData );
	pj_assert( tStatus == PJ_SUCCESS );

	tStatus = pjsip_dlg_send_request( p_psoDialog, psoTxData, -1, NULL );
	pj_assert( tStatus == PJ_SUCCESS );
}

static void send_bye( SActionData *p_psoActionData )
{
	pjsip_tx_data *psoTxData;
	pj_status_t tStatus;

	if( PJ_TRUE == p_psoActionData->m_bIsNeedRegisterThread ) {
		pj_thread_desc pjtdTreadDescr;
		pj_thread_t *pjThread;

		if( PJ_SUCCESS == pj_thread_register( __FUNCTION__, pjtdTreadDescr, &pjThread ) ) {
			UTL_LOG_D( *g_pcoLog, "thread was registered in pj successfully" );
		} else {
			UTL_LOG_D( *g_pcoLog, "can not register thread in pj" );
			goto __free_allocated_objects__;
		}
	}

	tStatus = pjsip_dlg_create_request( p_psoActionData->m_psoDialog, &pjsip_bye_method, p_psoActionData->m_i32CSec, &psoTxData );
	pj_assert( tStatus == PJ_SUCCESS );

	tStatus = pjsip_dlg_send_request( p_psoActionData->m_psoDialog, psoTxData, -1, NULL );
	pj_assert( tStatus == PJ_SUCCESS );

__free_allocated_objects__:
	if( NULL != p_psoActionData ) {
		delete p_psoActionData;
	}
}

static void send_resp( pjsip_dialog *p_psoDialog, pjsip_rx_data *p_psoRxData, int p_iStatus, const char *p_pszStatus )
{
	pjsip_tx_data *psoTxData;
	pj_status_t tStatus;
	pj_str_t strStatus = pj_str( const_cast< char* >( p_pszStatus ) );
	pjsip_transaction *psoTrans = pjsip_rdata_get_tsx( p_psoRxData );

	tStatus = pjsip_endpt_create_response( g_psoEndPoint, p_psoRxData, 200, &strStatus, &psoTxData );
	if( PJ_SUCCESS == tStatus ) {
	} else {
		return;
	}

	tStatus = pjsip_endpt_send_response2( g_psoEndPoint, p_psoRxData, psoTxData, NULL, NULL );
	if( PJ_SUCCESS == tStatus ) {
	} else {
		return;
	}
}

static void ca_hang_up( void *p_pvArg )
{
	SActionData *psoArg = reinterpret_cast< SActionData* >( p_pvArg );

	ca_sync_ulck( &psoArg->m_psoDialog->call_id->id, psoArg->m_iStatus, reinterpret_cast< void(*)( void* ) >( send_bye ), psoArg );
}
