all: main plugins

include config.mk
include scripts/lib.mk

CFLAGS	+= -I. -g

# programs {{{
cmus-y := \
	browser.o cmdline.o cmus.o command_mode.o comment.o \
	db.o debug.o editable.o expr.o filters.o \
	format_print.o glob.o history.o http.o input.o \
	keys.o lib.o load_dir.o locking.o mergesort.o misc.o options.o \
	output.o pcm.o pl.o play_queue.o player.o \
	read_wrapper.o server.o search.o \
	search_mode.o spawn.o tabexp.o tabexp_file.o \
	track.o track_db.o track_info.o tree.o uchar.o ui_curses.o window.o \
	worker.o xstrjoin.o

$(cmus-y): CFLAGS += $(PTHREAD_CFLAGS) $(NCURSES_CFLAGS) $(ICONV_CFLAGS)

cmus: $(cmus-y) file.o path.o prog.o xmalloc.o
	$(call cmd,ld,$(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) -lm)

cmus-remote: main.o file.o path.o prog.o xmalloc.o
	$(call cmd,ld,)
# }}}

# input plugins {{{
flac-objs		:= flac.lo
mad-objs		:= id3.lo mad.lo nomad.lo utf8_encode.lo
modplug-objs		:= modplug.lo
mpc-objs		:= mpc.lo
vorbis-objs		:= vorbis.lo
wav-objs		:= wav.lo

ip-$(CONFIG_FLAC)	+= flac.so
ip-$(CONFIG_MAD)	+= mad.so
ip-$(CONFIG_MODPLUG)	+= modplug.so
ip-$(CONFIG_MPC)	+= mpc.so
ip-$(CONFIG_VORBIS)	+= vorbis.so
ip-$(CONFIG_WAV)	+= wav.so

$(flac-objs):		CFLAGS += $(FLAC_CFLAGS)
$(mad-objs):		CFLAGS += $(MAD_CFLAGS)
$(modplug-objs):	CFLAGS += $(MODPLUG_CFLAGS)
$(mpc-objs):		CFLAGS += $(MPC_CFLAGS)
$(vorbis-objs):		CFLAGS += $(VORBIS_CFLAGS)

flac.so: $(flac-objs)
	$(call cmd,ld_so,$(FLAC_LIBS))

mad.so: $(mad-objs)
	$(call cmd,ld_so,$(MAD_LIBS))

modplug.so: $(modplug-objs)
	$(call cmd,ld_so,$(MODPLUG_LIBS))

mpc.so: $(mpc-objs)
	$(call cmd,ld_so,$(MPC_LIBS))

vorbis.so: $(vorbis-objs)
	$(call cmd,ld_so,$(VORBIS_LIBS))

wav.so: $(wav-objs)
	$(call cmd,ld_so,)
# }}}

# output plugins {{{
alsa-objs		:= alsa.lo mixer_alsa.lo
arts-objs		:= arts.lo
oss-objs		:= oss.lo mixer_oss.lo
sun-objs		:= sun.lo mixer_sun.lo
ao-objs			:= ao.lo

op-$(CONFIG_ALSA)	+= alsa.so
op-$(CONFIG_ARTS)	+= arts.so
op-$(CONFIG_OSS)	+= oss.so
op-$(CONFIG_SUN)	+= sun.so
op-$(CONFIG_AO)		+= ao.so

$(alsa-objs): CFLAGS	+= $(ALSA_CFLAGS)
$(arts-objs): CFLAGS	+= $(ARTS_CFLAGS)
$(oss-objs):  CFLAGS	+= $(OSS_CFLAGS)
$(sun-objs):  CFLAGS	+= $(SUN_CFLAGS)
$(ao-objs):   CFLAGS	+= $(AO_CFLAGS)

alsa.so: $(alsa-objs)
	$(call cmd,ld_so,$(ALSA_LIBS))

arts.so: $(arts-objs)
	$(call cmd,ld_so,$(ARTS_LIBS))

oss.so: $(oss-objs)
	$(call cmd,ld_so,$(OSS_LIBS))

sun.so: $(sun-objs)
	$(call cmd,ld_so,$(SUN_LIBS))

ao.so: $(ao-objs)
	$(call cmd,ld_so,$(AO_LIBS))
# }}}

data		= $(wildcard data/*)

clean		+= *.o *.lo *.so cmus cmus-remote *.html
distclean	+= config.mk config.h tags

main: cmus cmus-remote
plugins: $(ip-y) $(op-y)

install-main: main
	$(INSTALL) -m755 $(bindir) cmus cmus-remote
	$(INSTALL) -m644 $(datadir)/cmus $(data)
	$(INSTALL) -m755 $(datadir)/doc/cmus/examples cmus-status-display

install-plugins: plugins
	$(INSTALL) -m755 $(libdir)/cmus/ip $(ip-y)
	$(INSTALL) -m755 $(libdir)/cmus/op $(op-y)

install: main plugins install-main install-plugins

tags:
	exuberant-ctags *.[ch]

doc: man html
man: Doc/man
html: Doc/html
install-doc: install-man install-html
install-man: Doc/install-man
install-html: Doc/install-html
clean: Doc/clean

Doc/%:
	$(call cmd,submake)

quiet_cmd_submake = Making $(word 2,$(subst /, ,$@)) in $(word 1,$(subst /, ,$@))
      cmd_submake = $(MAKE) -C $(subst /, ,$@)

MAKEFLAGS += --no-print-directory

# generating tarball using GIT {{{
REV	= HEAD

# version from an annotated tag
_ver0	= $(shell git-describe $(REV) 2>/dev/null)
# version from a plain tag
_ver1	= $(shell git-describe --tags $(REV) 2>/dev/null)
# SHA1
_ver2	= $(shell git-rev-parse --verify $(REV) 2>/dev/null)

TARNAME	= $(PACKAGE)-$(if $(_ver0),$(_ver0),$(if $(_ver1),$(_ver1),$(_ver2)))

dist:
	@tarname=$(TARNAME);						\
	sha1=$(_ver2);							\
	test "$$sha1" || { echo "No such revision $(REV)"; exit 1; };	\
	echo "   DIST   $$tarname.tar.bz2";				\
	git-tar-tree $$sha1 $$tarname > $$tarname.tar;			\
	if test "$(REV)" = HEAD; then					\
		$(MAKE) doc;						\
		ln -s . $$tarname;					\
		tar rf $$tarname.tar $$tarname/Doc/{cmus,cmus-remote}.{1,html};	\
		rm $$tarname;						\
	fi;								\
	bzip2 -9 $$tarname.tar

# }}}

.PHONY: all main plugins doc man html dist tags
.PHONY: install install-main install-plugins install-doc install-man install-html

# If config.mk changes, rebuild all sources that include debug.h
#
# debug.h depends on DEBUG variable which is defined in config.mk
# if config.mk is newer than debug.h then touch debug.h
_dummy	:= $(shell test config.mk -nt debug.h && touch debug.h)
