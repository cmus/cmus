obj-y := \
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
	output.o \
	pcm.o \
	pl.o \
	play_queue.o \
	player.o \
	pls.o \
	server.o \
	sconf.o \
	search.o \
	search_mode.o \
	spawn.o \
	tabexp.o \
	tabexp_file.o \
	track_db.o \
	track_info.o \
	uchar.o \
	ui_curses.o \
	window.o \
	worker.o \
	xstrjoin.o

obj-$(CONFIG_FLAC)	+= ip_flac.o
obj-$(CONFIG_MAD)	+= ip_mad.o nomad.o
obj-$(CONFIG_MODPLUG)	+= ip_modplug.o
obj-$(CONFIG_VORBIS)	+= ip_vorbis.o
obj-$(CONFIG_WAV)	+= ip_wav.o
obj-$(CONFIG_ALSA)	+= mixer_alsa.o op_alsa.o
obj-$(CONFIG_ARTS)	+= op_arts.o
obj-$(CONFIG_OSS)	+= mixer_oss.o op_oss.o
obj-$(CONFIG_IRMAN)	+= irman.o irman_config.o
ifeq ($(CONFIG_MAD),y)
  obj-y			+= read_wrapper.o
else
  ifeq ($(CONFIG_VORBIS),y)
    obj-y		+= read_wrapper.o
  endif
endif

CFLAGS += -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g $(PTHREAD_CFLAGS) $(NCURSES_CFLAGS) $(ICONV_CFLAGS)
CFLAGS += $(FLAC_CFLAGS) $(MAD_CFLAGS) $(MODPLUG_CFLAGS) $(VORBIS_CFLAGS) $(ALSA_CFLAGS) $(ARTS_CFLAGS)

cmus: $(obj-y) $(top_builddir)/common/common.a
	$(call cmd,ld,$(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) -lm $(FAAD_LIBS) $(FLAC_LIBS) $(MAD_LIBS) $(MODPLUG_LIBS) $(VORBIS_LIBS) $(ALSA_LIBS) $(ARTS_LIBS))

install-exec:
	$(INSTALL) --auto cmus

targets	:= cmus

# If config.mk changes, rebuild all sources that include debug.h
#
# debug.h depends on DEBUG variable which is defined in config.mk
# if config.mk is newer than debug.h then touch debug.h
_dummy	:= $(shell [ $(top_builddir)/config.mk -nt $(srcdir)/debug.h ] && touch $(srcdir)/debug.h)
