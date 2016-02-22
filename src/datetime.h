#ifndef DATETIME_H
#define DATETIME_H

#ifndef _MSC_VER

#include <sys/time.h>

#else

#include <Windows.h>

void gettimeofday(struct timeval* tv, struct timezone* tz);

#endif

#endif