/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@freediameter.net>							 *
*													 *
* Copyright (c) 2013, WIDE Project and NICT								 *
* All rights reserved.											 *
* 													 *
* Redistribution and use of this software in source and binary forms, with or without modification, are  *
* permitted provided that the following conditions are met:						 *
* 													 *
* * Redistributions of source code must retain the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer.										 *
*    													 *
* * Redistributions in binary form must reproduce the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer in the documentation and/or other						 *
*   materials provided with the distribution.								 *
* 													 *
* * Neither the name of the WIDE Project or NICT nor the 						 *
*   names of its contributors may be used to endorse or 						 *
*   promote products derived from this software without 						 *
*   specific prior written permission of WIDE Project and 						 *
*   NICT.												 *
* 													 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A *
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 	 *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 	 *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR *
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF   *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.								 *
*********************************************************************************************************/

/* For development only : */
%debug 
%error-verbose

/* The parser receives the configuration file filename as parameter */
%parse-param {char * conffile}

/* Keep track of location */
%locations 
%pure-parser

%{
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>

#include "ca_conf.h"
#include "ca_caller_pool.h"
#include "../../utils/src/utils/log/log.h"
#include "ca_conf.tab.h"	/* bison is not smart enough to define the YYLTYPE before including this code, so... */

extern CLog *g_pcoLog;
extern SCAConf *g_psoConf;

/* Forward declaration */
int yyparse (char * conffile);

/* Parse the configuration file */
int ca_conf_handle (char * conffile)
{
	extern FILE * ca_confin;
	int ret;

	UTL_LOG_N( *g_pcoLog, "Parsing configuration file: %s...", conffile );

	ca_confin = fopen (conffile, "r");
	if (ca_confin == NULL) {
		ret = errno;
		UTL_LOG_D( *g_pcoLog, "Unable to open extension configuration file %s for reading: %s", conffile, strerror(ret));
		UTL_LOG_N( *g_pcoLog, "Error occurred, message logged -- configuration file.");
		return ret;
	}

	ret = yyparse (conffile);

	fclose (ca_confin);

	if (ret != 0) {
		UTL_LOG_N( *g_pcoLog, "Unable to parse the configuration file.");
		return EINVAL;
	}

	return 0;
}

/* The Lex parser prototype */
int ca_conflex (YYSTYPE *lvalp, YYLTYPE *llocp);

/* Function to report the errors */
void yyerror (YYLTYPE *ploc, char * conffile, char const *s)
{
	UTL_LOG_N( *g_pcoLog, "Error in configuration parsing");

	if (ploc->first_line != ploc->last_line)
		UTL_LOG_D( *g_pcoLog, "%s:%d.%d-%d.%d : %s", conffile, ploc->first_line, ploc->first_column, ploc->last_line, ploc->last_column, s);
	else if (ploc->first_column != ploc->last_column)
		UTL_LOG_D( *g_pcoLog, "%s:%d.%d-%d : %s", conffile, ploc->first_line, ploc->first_column, ploc->last_column, s);
	else
		UTL_LOG_D( *g_pcoLog, "%s:%d.%d : %s", conffile, ploc->first_line, ploc->first_column, s);
}

%}

/* Values returned by lex for token */
%union {
	char	*string;	/* The string is allocated by strdup in lex.*/
	int		 integer;	/* Store integer values */
	double	 dfValue;   /* Store double values */
}

/* In case of error in the lexical analysis */
%token 		LEX_ERROR

/* Key words */
%token 		SIP_SRV_ADR
%token 		SIP_SRV_PORT
%token 		SIP_LOCAL_ADDR
%token 		SIP_LOCAL_PORT
%token 		SIP_AUTH_REALM
%token 		SIP_AUTH_SCHEME
%token 		SIP_AUTH_USERNAME
%token 		SIP_AUTH_PASSWORD
%token 		SIP_TIMEOUT
%token 		SIP_RINGING_TIMEOUT
%token 		LOG_FILE_MASK
%token 		CALLER
%token 		TCP_LISTENER_ADDR
%token 		TCP_LISTENER_PORT
%token 		TCP_LISTENER_QUEUE_LEN
%token 		TCP_LISTENER_THREAD_COUNT

/* Tokens and types for routing table definition */
/* A (de)quoted string (malloc'd in lex parser; it must be freed after use) */
%token <string>	QSTRING

/* An integer value */
%token <integer> INTEGER

/* An double value */
%token <dfValue> DOUBLE


/* -------------------------------------- */
%%

	/* The grammar definition */
conffile:		/* empty grammar is OK */
			| conffile SIPSrvAdr
			| conffile SIPSrvPort
			| conffile SIPLocalAddr
			| conffile SIPLocalPort
			| conffile SIPAuthRealm
			| conffile SIPAuthScheme
			| conffile SIPAuthUserName
			| conffile SIPAuthPassword
			| conffile SIPTimeout
			| conffile SIPRingingTimeout
			| conffile LogFileMask
			| conffile Caller
			| conffile TCPListenerAddress
			| conffile TCPListenerPort
			| conffile TCPListenerQueueLen
			| conffile TCPListenerThreadCount
			;

SIPSrvAdr:	SIP_SRV_ADR '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPSrvAdr );
				g_psoConf->m_pszSIPSrvAdr = $3;
			}
			;

SIPSrvPort:	SIP_SRV_PORT '=' INTEGER ';'
			{
				g_psoConf->m_usSIPSrvPort = $3;
			}
			;

SIPLocalAddr:	SIP_LOCAL_ADDR '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPLocalAddr );
				g_psoConf->m_pszSIPLocalAddr = $3;
			}
			;

SIPLocalPort:	SIP_LOCAL_PORT '=' INTEGER ';'
			{
				g_psoConf->m_usSIPLocalPort = $3;
			}
			;

SIPAuthRealm:		SIP_AUTH_REALM '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPAuthRealm );
				g_psoConf->m_pszSIPAuthRealm = $3;
			}
			;

SIPAuthScheme:		SIP_AUTH_SCHEME '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPAuthScheme );
				g_psoConf->m_pszSIPAuthScheme = $3;
			}
			;

SIPAuthUserName:		SIP_AUTH_USERNAME '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPAuthUserName );
				g_psoConf->m_pszSIPAuthUserName = $3;
			}
			;

SIPAuthPassword:		SIP_AUTH_PASSWORD '=' QSTRING ';'
			{
				free( g_psoConf->m_pszSIPAuthPassword );
				g_psoConf->m_pszSIPAuthPassword = $3;
			}
			;

SIPTimeout:		SIP_TIMEOUT '=' INTEGER ';'
			{
				g_psoConf->m_uiSIPTimeout = $3;
			}
			;

SIPRingingTimeout:		SIP_RINGING_TIMEOUT '=' DOUBLE ';'
			{
				double dfIntergral;
				g_psoConf->m_uiRingingUSec = modf( $3, &dfIntergral ) * 1000000;
				g_psoConf->m_uiRingingSec = static_cast<int>( dfIntergral );
			}
			;

LogFileMask:		LOG_FILE_MASK '=' QSTRING ';'
			{
				free( g_psoConf->m_pszLogFileMask );
				g_psoConf->m_pszLogFileMask = $3;
			}
			;

Caller:		CALLER '=' QSTRING ';'
			{
				ca_caller_pool_add( $3 );
			}
			;

TCPListenerAddress:		TCP_LISTENER_ADDR '=' QSTRING ';'
			{
				free( g_psoConf->m_pszTCPListenerAddr );
				g_psoConf->m_pszTCPListenerAddr = $3;
			}
			;

TCPListenerPort:		TCP_LISTENER_PORT '=' INTEGER ';'
			{
				g_psoConf->m_usTCPLIstenerPort = static_cast<unsigned short>( $3 );
			}
			;

TCPListenerQueueLen:		TCP_LISTENER_QUEUE_LEN '=' INTEGER ';'
			{
				g_psoConf->m_iTCPListenerQueueLen = $3;
			}
			;

TCPListenerThreadCount:		TCP_LISTENER_THREAD_COUNT '=' INTEGER ';'
			{
				g_psoConf->m_iTCPListenerThreadCount = $3;
			}
			;
