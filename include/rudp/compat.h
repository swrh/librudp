#ifndef _RUDP_COMPAT_H_
#define _RUDP_COMPAT_H_

#ifdef _MSC_VER
# include <winsock2.h>
# include <ws2tcpip.h>
# include <basetsd.h>
#else
# include <sys/types.h>
# include <sys/time.h>
# include <sys/socket.h>
# include <netinet/in.h>
#endif

#endif
