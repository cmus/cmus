objs-y := \
	browser.o \
	cmus.o \
	command_mode.o \
	comment.o \
	db.o \
	debug.o \
	file_load.o \
	format_print.o \
	history.o \
	http.o \
	input.o \
	load_dir.o \
	mergesort.o \
	misc.o \
	options.o \
	output.o \
	pcm.o \
	pl.o \
	play_queue.o \
	player.o \
	pls.o \
	read_wrapper.o \
	server.o \
	sconf.o \
	search.o \
	search_mode.o \
	spawn.o \
	symbol.o \
	tabexp.o \
	tabexp_file.o \
	track_db.o \
	track_info.o \
	uchar.o \
	ui_curses.o \
	window.o \
	worker.o \
	xstrjoin.o

objs-$(CONFIG_IRMAN)	+= irman.o irman_config.o

CFLAGS += -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g $(PTHREAD_CFLAGS) $(NCURSES_CFLAGS) $(ICONV_CFLAGS)

cmus: $(objs-y) $(top_builddir)/common/common.a
	$(call cmd,ld,$(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) -lm)

install-exec:
	$(INSTALL) --fmode=0755 $(bindir) cmus

targets-y	+= cmus
clean		+= $(objs-y) $(objs-n)

# If config.mk changes, rebuild all sources that include debug.h
#
# debug.h depends on DEBUG variable which is defined in config.mk
# if config.mk is newer than debug.h then touch debug.h
_dummy	:= $(shell [ $(top_builddir)/config.mk -nt $(srcdir)/debug.h ] && touch $(srcdir)/debug.h)
