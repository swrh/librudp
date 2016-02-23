
lib_LTLIBRARIES = librudp.la

librudp_la_SOURCES = address.c server.c rudp_list.h peer.c	\
endpoint.c client.c packet.c rudp.c rudp_rudp.h rudp_error.h rudp_packet.h \
ela.c ela_libevent.c
librudp_la_CFLAGS = -I$(top_srcdir)/src	\
-I$(top_srcdir)/include $(GCC_CFLAGS) \
$(LIBEVENT_CFLAGS)
librudp_la_LIBADD = $(LIBEVENT_LIBS)
