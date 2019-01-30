#include <string.h>

#include "ca_conf.h"

static SCAConf g_soConf;
SCAConf *g_psoConf;

void ca_conf_init()
{
	memset( &g_soConf, 0, sizeof( g_soConf ) );
	g_psoConf = &g_soConf;
}
