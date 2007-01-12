## Process this file with automake to produce Makefile.in

EXTRA_DIST =

INCLUDES = @GTK_CFLAGS@ @GLIB_CFLAGS@

AM_CFLAGS = $(PLATFORM_CFLAGS) -DDATADIR=\"$(datadir)\"

bin_PROGRAMS = galarm
galarm_SOURCES = main.c galarm_config.c galarm_config.h
galarm_LDADD = @GTK_LIBS@ @GLIB_LIBS@

# install symlink
install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	( cd $(DESTDIR)$(bindir) ; ln -sf galarm ga )

uninstall-local:
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	( cd $(DESTDIR)$(bindir) ; rm ga )
