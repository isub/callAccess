#include <stddef.h>

#include "ca.h"

#define USEC_PER_SEC	1000000
#define NSEC_PER_USEC	1000

int ca_make_timespec_timeout( timespec &p_soTimeSpec, unsigned int p_uiSec, unsigned int p_uiUSec )
{
	timeval soTimeVal;

	if( 0 == gettimeofday( &soTimeVal, NULL ) ) {
	} else {
		return -1;
	}
	p_soTimeSpec.tv_sec = soTimeVal.tv_sec;
	p_soTimeSpec.tv_sec += p_uiSec;
	if( ( soTimeVal.tv_usec + p_uiUSec ) < USEC_PER_SEC ) {
		p_soTimeSpec.tv_nsec = ( soTimeVal.tv_usec + p_uiUSec ) * NSEC_PER_USEC;
	} else {
		++p_soTimeSpec.tv_sec;
		p_soTimeSpec.tv_nsec = ( soTimeVal.tv_usec - USEC_PER_SEC + p_uiUSec ) * NSEC_PER_USEC;
	}

	return 0;
}
