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

/* Lex configuration parser.
 *
 * This file defines the token for parsing the daemon's configuration file
 * Note that each extension has a separate independant configuration file.
 *
 * Note : This module is NOT thread-safe. All processing must be done from one thread only.
 */
%{

#include "../../utils/src/utils/log/log.h"
#include "ca_conf.h"

/* Include yacc tokens definitions */
#include "ca_conf.tab.h"

extern CLog *g_pcoLog;

/* Update the column information */
#ifdef DEBUG_LEX
#define YY_USER_ACTION { 						\
	yylloc->first_column = yylloc->last_column + 1; 		\
	yylloc->last_column = yylloc->first_column + yyleng - 1;	\
	fd_log_debug(	 						\
		"(%d:%d-%d:%d) matched rule %d, length=%d, txt='%s'",	\
		yylloc->first_line, yylloc->first_column, 		\
		yylloc->last_line, yylloc->last_column, 		\
		yy_act, yyleng, yytext); 				\
}
#else /* DEBUG_LEX */
#define YY_USER_ACTION { 						\
	yylloc->first_column = yylloc->last_column + 1; 		\
	yylloc->last_column = yylloc->first_column + yyleng - 1;	\
}
#endif

/* %option noinput ? */
#define YY_NO_INPUT

/* Additional for files inclusion */
#include <glob.h>
#include <string.h>

#define MAX_NESTED_CONF_FILES	5

struct nested_conffiles_t {
	YY_BUFFER_STATE parent_level_state;
	glob_t filelist;
	int current_file;
} nested_conffiles[MAX_NESTED_CONF_FILES];

int current_nested_level = 0;

int globerrfct(const char *epath, int eerrno)
{
	UTL_LOG_E( *g_pcoLog, "Failed to scan %s: %s", epath, strerror(eerrno));
	return 1;
}

%}

%option bison-bridge bison-locations
%option noyywrap
%option nounput

%x in_include

/* Quoted string. Multilines do not match. */
qstring		\"[^\"\n]*\"

%%
<*>\n			{ 
				/* Update the line count */
				yylloc->first_line++; 
				yylloc->last_line++; 
				yylloc->last_column=0; 
			} 

<*>([[:space:]]{-}[\n])+	; /* Eat all spaces, not new lines */
<*>#.*$			; /* Eat all comments */


include		BEGIN(in_include);
	/* Following an "include" keyword */
<in_include>{
{qstring}	{ /* Name of the file to include. This is directly sent to glob. */
			int globerror=0;
			char * buf = strdup(yytext+1);
			if (buf[yyleng-2] != '"')
			{
				UTL_LOG_E( *g_pcoLog, "Unterminated string: %s", yytext);
				return LEX_ERROR;
			}
			buf[yyleng-2] = '\0';

			if (current_nested_level >= MAX_NESTED_CONF_FILES)
			{
				UTL_LOG_E( *g_pcoLog, "Too many recursion levels in configuration files includes");
				return LEX_ERROR;
			}

			/* glob the include */
			globerror = glob(buf, GLOB_ERR, globerrfct, &nested_conffiles[current_nested_level].filelist);

			if (globerror == GLOB_NOSPACE)
			{
				UTL_LOG_E( *g_pcoLog, "Not enough memory to parse include directive.");
				return LEX_ERROR;
			}
			if (globerror == GLOB_ABORTED)
			{
				UTL_LOG_E( *g_pcoLog, "An error was encountered in include directive.");
				return LEX_ERROR;
			}
			if (globerror == GLOB_NOMATCH)
			{
				globfree(&nested_conffiles[current_nested_level].filelist);
				goto nomatch;
			}
			if (globerror)
			{
				UTL_LOG_E( *g_pcoLog, "Unexpected error in glob (%d).", globerror);
				return LEX_ERROR;
			}

			/* We have a list of files to include. */

			/* save the current buffer for returning when this include has been parsed */
			nested_conffiles[current_nested_level].parent_level_state = YY_CURRENT_BUFFER;

			/* Start with the first match */
			nested_conffiles[current_nested_level].current_file = 0;

			yyin = fopen( nested_conffiles[current_nested_level].filelist.gl_pathv[0], "r" );

			if ( ! yyin )
			{
				UTL_LOG_E( *g_pcoLog, "Error in %s: %s", nested_conffiles[current_nested_level].filelist.gl_pathv[0], strerror(errno));
				return LEX_ERROR;
			}

			yy_switch_to_buffer(yy_create_buffer( yyin, YY_BUF_SIZE ));

			/* In case of recursive includes */
			current_nested_level++;

nomatch:
			BEGIN(INITIAL);
		}
}

<<EOF>>	{
			if (current_nested_level == 0)
			{
			      /* We are at the end of parsing */
			      yyterminate();
			}

			/* Otherwise we are doing an include statement */
			--current_nested_level;
			yy_delete_buffer(YY_CURRENT_BUFFER);

			/* Go to next file, if any */
			nested_conffiles[current_nested_level].current_file++;
			if ( nested_conffiles[current_nested_level].filelist.gl_pathv[nested_conffiles[current_nested_level].current_file] == NULL )
			{
				/* We have finished with this list of includes */
				globfree(&nested_conffiles[current_nested_level].filelist);
				yy_switch_to_buffer(nested_conffiles[current_nested_level].parent_level_state);
			}
			else
			{
				/* Proceed to next included file */
				yyin = fopen( nested_conffiles[current_nested_level].filelist.gl_pathv[nested_conffiles[current_nested_level].current_file], "r" );

				if ( ! yyin )
				{
					UTL_LOG_E( *g_pcoLog, "Error in %s: %s", nested_conffiles[current_nested_level].filelist.gl_pathv[nested_conffiles[current_nested_level].current_file], strerror(errno));
					return LEX_ERROR;
				}

				yy_switch_to_buffer(yy_create_buffer( yyin, YY_BUF_SIZE ));

				/* In case of recursive includes */
				current_nested_level++;
			}

}

{qstring}		{
				/* First copy the string without the quotes for use in the yacc parser */
				yylval->string = strdup(yytext+1);
				if( NULL != yylval->string ) {
				} else {
					return LEX_ERROR;
				}

				yylval->string[yyleng-2] = '\0';
				
				/* the yacc parser will check the string is valid */
				return QSTRING;
			}
			
[[:digit:]]+		{
				/* Convert this to an integer value */
				int ret = sscanf(yytext, "%i", &yylval->integer);
				if (ret != 1) {
					/* No matching: an error occurred */
					UTL_LOG_E( *g_pcoLog, "Unable to convert the value '%s' to a valid number: %s", yytext, strerror(errno));
					return LEX_ERROR; /* trig an error in yacc parser */
					/* Maybe we could REJECT instead of failing here? */
				}
				return INTEGER;
			}

[0-9]+\.[0-9]+		{
				/* Convert this to an integer value */
				int ret = sscanf(yytext, "%lf", &yylval->dfValue);
				if (ret != 1) {
					/* No matching: an error occurred */
					UTL_LOG_E( *g_pcoLog, "Unable to convert the value '%s' to a valid number: %s", yytext, strerror(errno));
					return LEX_ERROR; /* trig an error in yacc parser */
					/* Maybe we could REJECT instead of failing here? */
				}
				return DOUBLE;
			}
				
	/* Full words tokens (keywords) */
(?i:"SIPSrvAdr")				{ return SIP_SRV_ADR; }
(?i:"SIPSrvPort")				{ return SIP_SRV_PORT; }
(?i:"SIPLocalAddr")				{ return SIP_LOCAL_ADDR; }
(?i:"SIPLocalPort")				{ return SIP_LOCAL_PORT; }
(?i:"SIPAuthRealm")				{ return SIP_AUTH_REALM; }
(?i:"SIPAuthScheme")			{ return SIP_AUTH_SCHEME; }
(?i:"SIPAuthUserName")			{ return SIP_AUTH_USERNAME; }
(?i:"SIPAuthPassword")			{ return SIP_AUTH_PASSWORD; }
(?i:"SIPTimeout")				{ return SIP_TIMEOUT; }
(?i:"SIPRingingTimeout")		{ return SIP_RINGING_TIMEOUT; }
(?i:"LogFileMask")				{ return LOG_FILE_MASK; }
(?i:"Caller")					{ return CALLER; }
(?i:"TCPListenerAddress")		{ return TCP_LISTENER_ADDR; }
(?i:"TCPListenerPort")			{ return TCP_LISTENER_PORT; }
(?i:"TCPListenerQueueLen")		{ return TCP_LISTENER_QUEUE_LEN; }
(?i:"TCPListenerThreadCount")	{ return TCP_LISTENER_THREAD_COUNT; }


	/* Valid single characters for yyparse */
<*>[=,:;{}]		{ return yytext[0]; }

	/* Unrecognized token */
<*>[[:alnum:]]+		|	/* This rule is only useful to print a complete token in error messages */
	/* Unrecognized character */
<*>.			{
				UTL_LOG_E( *g_pcoLog, "Unrecognized text on line %d col %d: '%s'.", yylloc->first_line, yylloc->first_column, yytext);
			 	return LEX_ERROR; 
			}

%%
