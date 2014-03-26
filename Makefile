REV	= HEAD

# version from an annotated tag
_ver0	= $(shell git describe $(REV) 2>/dev/null)
# version from a plain tag
_ver1	= $(shell git describe --tags $(REV) 2>/dev/null)
# SHA1
_ver2	= $(shell git rev-parse --verify --short $(REV) 2>/dev/null)
# hand-made
_ver3	= v2.5.0

VERSION	= $(or $(_ver0),$(_ver1),$(_ver2),$(_ver3))

all: main plugins man

include config.mk
include scripts/lib.mk

CFLAGS += -D_FILE_OFFSET_BITS=64

FFMPEG_CFLAGS += $(shell pkg-config --cflags libswresample)
FFMPEG_LIBS += $(shell pkg-config --libs libswresample)

CMUS_LIBS = $(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) $(DISCID_LIBS) $(CUE_LIBS) -lm $(COMPAT_LIBS)

input.o main.o ui_curses.o pulse.lo: .version
input.o main.o ui_curses.o pulse.lo: CFLAGS += -DVERSION=\"$(VERSION)\"
main.o server.o: CFLAGS += -DDEFAULT_PORT=3000
discid.o: CFLAGS += $(DISCID_CFLAGS)
job.o cue_utils.o: CFLAGS += $(CUE_CFLAGS)

.version: Makefile
	@test "`cat $@ 2> /dev/null`" = "$(VERSION)" && exit 0; \
	echo "   GEN    $@"; echo $(VERSION) > $@

# programs {{{
cmus-y := \
	ape.o browser.o buffer.o cache.o cmdline.o cmus.o command_mode.o comment.o \
	channelmap.o convert.lo debug.o discid.o editable.o expr.o filters.o \
	format_print.o gbuf.o glob.o help.o history.o http.o id3.o input.o job.o \
	keys.o keyval.o lib.o load_dir.o locking.o mergesort.o misc.o options.o \
	output.o pcm.o pl.o play_queue.o player.o \
	rbtree.o read_wrapper.o server.o search.o \
	search_mode.o spawn.o tabexp.o tabexp_file.o \
	track.o track_info.o tree.o u_collate.o uchar.o ui_curses.o \
	window.o worker.o xstrjoin.o

cmus-$(CONFIG_CUE) += cue_utils.o

$(cmus-y): CFLAGS += $(PTHREAD_CFLAGS) $(NCURSES_CFLAGS) $(ICONV_CFLAGS) $(DL_CFLAGS)

cmus: $(cmus-y) file.o path.o prog.o xmalloc.o
	$(call cmd,ld,$(CMUS_LIBS))

cmus-remote: main.o file.o misc.o path.o prog.o xmalloc.o xstrjoin.o
	$(call cmd,ld,$(COMPAT_LIBS))

# cygwin compat
DLLTOOL=dlltool

libcmus-$(CONFIG_CYGWIN) := libcmus.a

libcmus.a: $(cmus-y) file.o path.o prog.o xmalloc.o
	$(LD) -shared -o cmus.exe -Wl,--out-implib=libcmus.a -Wl,--base-file,cmus.base \
		-Wl,--export-all-symbols -Wl,--no-whole-archive $^ $(CMUS_LIBS)
	$(DLLTOOL) --output-def cmus.def --dllname cmus.exe --export-all-symbols $^
	$(DLLTOOL) --base-file cmus.base --dllname cmus.exe --input-def cmus.def --output-exp cmus.exp
	$(LD) -o cmus.exe -Wl,cmus.exp $^ $(CMUS_LIBS)

# }}}

# input plugins {{{
cdio-objs		:= cdio.lo
flac-objs		:= flac.lo
mad-objs		:= mad.lo nomad.lo
mikmod-objs		:= mikmod.lo
modplug-objs		:= modplug.lo
mpc-objs		:= mpc.lo
vorbis-objs		:= vorbis.lo
opus-objs		:= opus.lo
wavpack-objs		:= wavpack.lo
wav-objs		:= wav.lo
mp4-objs		:= mp4.lo
aac-objs		:= aac.lo
ffmpeg-objs		:= ffmpeg.lo
cue-objs		:= cue.lo

ip-$(CONFIG_CDIO)	+= cdio.so
ip-$(CONFIG_FLAC)	+= flac.so
ip-$(CONFIG_MAD)	+= mad.so
ip-$(CONFIG_MIKMOD)	+= mikmod.so
ip-$(CONFIG_MODPLUG)	+= modplug.so
ip-$(CONFIG_MPC)	+= mpc.so
ip-$(CONFIG_VORBIS)	+= vorbis.so
ip-$(CONFIG_OPUS)	+= opus.so
ip-$(CONFIG_WAVPACK)	+= wavpack.so
ip-$(CONFIG_WAV)	+= wav.so
ip-$(CONFIG_MP4)	+= mp4.so
ip-$(CONFIG_AAC)	+= aac.so
ip-$(CONFIG_FFMPEG)	+= ffmpeg.so
ip-$(CONFIG_CUE)	+= cue.so

$(cdio-objs):		CFLAGS += $(CDIO_CFLAGS) $(CDDB_CFLAGS)
$(flac-objs):		CFLAGS += $(FLAC_CFLAGS)
$(mad-objs):		CFLAGS += $(MAD_CFLAGS)
$(mikmod-objs):		CFLAGS += $(MIKMOD_CFLAGS)
$(modplug-objs):	CFLAGS += $(MODPLUG_CFLAGS)
$(mpc-objs):		CFLAGS += $(MPC_CFLAGS)
$(vorbis-objs):		CFLAGS += $(VORBIS_CFLAGS)
$(opus-objs):		CFLAGS += $(OPUS_CFLAGS)
$(wavpack-objs):	CFLAGS += $(WAVPACK_CFLAGS)
$(mp4-objs):		CFLAGS += $(MP4_CFLAGS)
$(aac-objs):		CFLAGS += $(AAC_CFLAGS)
$(ffmpeg-objs):		CFLAGS += $(FFMPEG_CFLAGS)
$(cue-objs):		CFLAGS += $(CUE_CFLAGS)

cdio.so: $(cdio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(CDIO_LIBS) $(CDDB_LIBS))

flac.so: $(flac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(FLAC_LIBS))

mad.so: $(mad-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MAD_LIBS) $(ICONV_LIBS))

mikmod.so: $(mikmod-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MIKMOD_LIBS))

modplug.so: $(modplug-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MODPLUG_LIBS))

mpc.so: $(mpc-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MPC_LIBS))

vorbis.so: $(vorbis-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(VORBIS_LIBS))

opus.so: $(opus-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(OPUS_LIBS))

wavpack.so: $(wavpack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(WAVPACK_LIBS))

wav.so: $(wav-objs) $(libcmus-y)
	$(call cmd,ld_dl,)

mp4.so: $(mp4-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MP4_LIBS))

aac.so: $(aac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(AAC_LIBS))

ffmpeg.so: $(ffmpeg-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(FFMPEG_LIBS))

cue.so: $(cue-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(CUE_LIBS))

# }}}

# output plugins {{{
pulse-objs		:= pulse.lo
alsa-objs		:= alsa.lo mixer_alsa.lo
jack-objs		:= jack.lo
arts-objs		:= arts.lo
oss-objs		:= oss.lo mixer_oss.lo
sun-objs		:= sun.lo mixer_sun.lo
ao-objs			:= ao.lo
waveout-objs		:= waveout.lo
roar-objs               := roar.lo

op-$(CONFIG_PULSE)	+= pulse.so
op-$(CONFIG_ALSA)	+= alsa.so
op-$(CONFIG_JACK)	+= jack.so
op-$(CONFIG_ARTS)	+= arts.so
op-$(CONFIG_OSS)	+= oss.so
op-$(CONFIG_SUN)	+= sun.so
op-$(CONFIG_AO)		+= ao.so
op-$(CONFIG_WAVEOUT)	+= waveout.so
op-$(CONFIG_ROAR)       += roar.so

$(pulse-objs): CFLAGS	+= $(PULSE_CFLAGS)
$(alsa-objs): CFLAGS	+= $(ALSA_CFLAGS)
$(jack-objs): CFLAGS	+= $(JACK_CFLAGS)
$(arts-objs): CFLAGS	+= $(ARTS_CFLAGS)
$(oss-objs):  CFLAGS	+= $(OSS_CFLAGS)
$(sun-objs):  CFLAGS	+= $(SUN_CFLAGS)
$(ao-objs):   CFLAGS	+= $(AO_CFLAGS)
$(waveout-objs): CFLAGS += $(WAVEOUT_CFLAGS)
$(roar-objs): CFLAGS	+= $(ROAR_CFLAGS)

pulse.so: $(pulse-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(PULSE_LIBS))

alsa.so: $(alsa-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ALSA_LIBS))

jack.so: $(jack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(JACK_LIBS))

arts.so: $(arts-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ARTS_LIBS))

oss.so: $(oss-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(OSS_LIBS))

sun.so: $(sun-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(SUN_LIBS))

ao.so: $(ao-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(AO_LIBS))

waveout.so: $(waveout-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(WAVEOUT_LIBS))

roar.so: $(roar-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ROAR_LIBS))
# }}}

# man {{{
man1	:= Doc/cmus.1 Doc/cmus-remote.1
man7	:= Doc/cmus-tutorial.7

$(man1): Doc/ttman
$(man7): Doc/ttman

%.1: %.txt
	$(call cmd,ttman)

%.7: %.txt
	$(call cmd,ttman)

Doc/ttman.o: Doc/ttman.c
	$(call cmd,hostcc,)

Doc/ttman: Doc/ttman.o
	$(call cmd,hostld,)

quiet_cmd_ttman = MAN    $@
      cmd_ttman = Doc/ttman $< $@
# }}}

data		= $(wildcard data/*)

clean		+= *.o *.lo *.so cmus libcmus.a cmus.def cmus.base cmus.exp cmus-remote Doc/*.o Doc/ttman Doc/*.1 Doc/*.7 .install.log
distclean	+= .version config.mk config/*.h tags

main: cmus cmus-remote
plugins: $(ip-y) $(op-y)
man: $(man1) $(man7)

install-main: main
	$(INSTALL) -m755 $(bindir) cmus cmus-remote

install-plugins: plugins
	$(INSTALL) -m755 $(libdir)/cmus/ip $(ip-y)
	$(INSTALL) -m755 $(libdir)/cmus/op $(op-y)

install-data: man
	$(INSTALL) -m644 $(datadir)/cmus $(data)
	$(INSTALL) -m644 $(mandir)/man1 $(man1)
	$(INSTALL) -m644 $(mandir)/man7 $(man7)
	$(INSTALL) -m755 $(exampledir) cmus-status-display

install: all install-main install-plugins install-data

tags:
	exuberant-ctags *.[ch]

# generating tarball using GIT {{{
TARNAME	= cmus-$(VERSION)

dist:
	@tarname=$(TARNAME);						\
	test "$(_ver2)" || { echo "No such revision $(REV)"; exit 1; };	\
	echo "   DIST   $$tarname.tar.bz2";				\
	git archive --format=tar --prefix=$$tarname/ $(REV)^{tree} | bzip2 -c -9 > $$tarname.tar.bz2

# }}}

.PHONY: all main plugins man dist tags
.PHONY: install install-main install-plugins install-man
