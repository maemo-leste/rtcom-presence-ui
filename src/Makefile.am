bin_PROGRAMS = rtcom-presence-ui

rtcom_presence_ui_CFLAGS = 						\
		$(PRESENCE_UI_CFLAGS) -I$(top_srcdir)/lib/

rtcom_presence_ui_LDFLAGS =						\
		-Wl,--as-needed $(PRESENCE_UI_LIBS)

rtcom_presence_ui_LDADD =							\
		$(top_builddir)/lib/librtcom-presence-ui.la

rtcom_presence_ui_SOURCES =						\
		pui.c

MAINTAINERCLEANFILES = Makefile.in
