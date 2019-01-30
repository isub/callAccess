#include <sys/time.h>

#ifndef __CA_H__
#define __CA_H__

int ca_make_timespec_timeout( timespec &p_soTimeSpec, unsigned int p_uiSec, unsigned int p_uiUSec );

#endif /* __CA_H__ */
