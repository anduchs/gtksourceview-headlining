@GNOME_CODE_COVERAGE_RULES@

INCLUDES = 				\
	-DDATADIR=\""$(datadir)"\" 	\
	-DG_LOG_DOMAIN=\"GtkSourceView\"\
	-I$(top_srcdir) -I$(srcdir) 	\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(WARN_CFLAGS) 			\
	$(DEP_CFLAGS)

noinst_LTLIBRARIES = libgtksourcecompletionwords.la

NOINST_H_FILES =				\
	gtksourcecompletionwordsbuffer.h	\
	gtksourcecompletionwordslibrary.h	\
	gtksourcecompletionwordsproposal.h	\
	gtksourcecompletionwordsutils.h

libgtksourcecompletionwords_headers = 		\
	gtksourcecompletionwords.h

libgtksourcecompletionwords_la_SOURCES =	\
	gtksourcecompletionwords.c		\
	gtksourcecompletionwordsbuffer.c	\
	gtksourcecompletionwordslibrary.c	\
	gtksourcecompletionwordsproposal.c	\
	gtksourcecompletionwordsutils.c		\
	$(libgtksourcecompletionwords_headers)	\
	$(NOINST_H_FILES)

libgtksourcecompletionwords_la_CFLAGS =		\
	$(CODE_COVERAGE_CFLAGS)

libgtksourcecompletionwords_la_LDFLAGS =	\
	$(CODE_COVERAGE_LDFLAGS)

libgtksourcecompletionwords_includedir = \
	$(includedir)/gtksourceview-3.0/gtksourceview/completion-providers/words

libgtksourcecompletionwords_include_HEADERS =		\
	$(libgtksourcecompletionwords_headers)

-include $(top_srcdir)/git.mk
