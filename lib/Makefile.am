librtcom_presence_ui_la_LTLIBRARIES = librtcom-presence-ui.la
librtcom_presence_ui_ladir = $(hildondesktoplibdir)

librtcom_presence_ui_la_CFLAGS = $(PRESENCE_UI_CFLAGS)

librtcom_presence_ui_la_LDFLAGS =					\
		-Wl,--as-needed $(PRESENCE_UI_LIBS) -Wl,--no-undefined

librtcom_presence_ui_la_SOURCES =					\
		pui-marshal.c						\
		pui-module.c						\
		pui-account-model.c					\
		pui-account-view.c					\
		pui-location.c						\
		pui-dbus.c						\
		pui-profile.c						\
		pui-master.c						\
		pui-main-view.c						\
		pui-list-picker.c

dbus-glib-marshal-presence-ui.h: $(top_srcdir)/xml/presence-ui.xml
	$(DBUS_BINDING_TOOL) --prefix=presence_ui			\
		--mode=glib-server $< > xgen-$(@F)			\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )			\
	&& rm -f xgen-$(@F)

pui-marshal.c: pui-marshal.list
	$(GLIB_GENMARSHAL) --prefix=pui_signal_marshal $< --header	\
	--body --internal > xgen-$(@F)	&& ( cmp -s xgen-$(@F) $@ ||	\
	cp xgen-$(@F) $@ ) && rm -f xgen-$(@F)

pui-marshal.h: pui-marshal.list
	$(GLIB_GENMARSHAL) --prefix=pui_signal_marshal $< --header	\
	--internal > xgen-$(@F) && ( cmp -s xgen-$(@F) $@ ||		\
	cp xgen-$(@F) $@ ) && rm -f xgen-$(@F)

BUILT_SOURCES =								\
		pui-marshal.c						\
		pui-marshal.h						\
		dbus-glib-marshal-presence-ui.h

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES = Makefile.in
