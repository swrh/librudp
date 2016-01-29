#ifndef COMPAT_H
#define COMPAT_H

#ifdef _MSC_VER

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <basetsd.h>

#else

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#endif

#if __STDC_VERSION__ >= 199901L

#define INLINE inline

#else

#define INLINE

#endif

#endif // COMPAT_H