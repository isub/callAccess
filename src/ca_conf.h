#ifndef __CA_CONF_H__
#define __CA_CONF_H__

struct SCAConf {
	char			*m_pszSIPLocalAddr;
	unsigned short	 m_usSIPLocalPort;

	char			*m_pszSIPSrvAdr;
	unsigned short	 m_usSIPSrvPort;

	char			*m_pszSIPAuthRealm;
	char			*m_pszSIPAuthScheme;
	char			*m_pszSIPAuthUserName;
	char			*m_pszSIPAuthPassword;

	unsigned int	 m_uiSIPTimeout;

	unsigned int	 m_uiRingingUSec;
	unsigned int	 m_uiRingingSec;

	char			*m_pszLogFileMask;

	char			*m_pszTCPListenerAddr;
	unsigned short	 m_usTCPLIstenerPort;
	int				 m_iTCPListenerQueueLen;
	int				 m_iTCPListenerThreadCount;
};

void ca_conf_init();

extern "C"
int ca_conf_handle( char *p_pszConfFileName );

#endif /* __CA_CONF_H__ */
