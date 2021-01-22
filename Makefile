REV	= HEAD

# version from an annotated tag
_ver0	= $(shell git describe $(REV) 2>/dev/null)
# version from a plain tag
_ver1	= $(shell git describe --tags $(REV) 2>/dev/null)
# SHA1
_ver2	= $(shell git rev-parse --verify --short $(REV) 2>/dev/null)
# hand-made
_ver3	= v2.9.1

VERSION	= $(or $(_ver0),$(_ver1),$(_ver2),$(_ver3))

all: main plugins man

-include config.mk
include scripts/lib.mk

CFLAGS += -D_FILE_OFFSET_BITS=64

FFMPEG_CFLAGS += $(shell pkg-config --cflags libswresample)
FFMPEG_LIBS += $(shell pkg-config --libs libswresample)

CMUS_LIBS = $(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) $(DISCID_LIBS) \
			-lm $(COMPAT_LIBS) $(LIBSYSTEMD_LIBS)

command_mode.o input.o main.o ui_curses.o op/pulse.lo: .version
command_mode.o input.o main.o ui_curses.o op/pulse.lo: CFLAGS += -DVERSION=\"$(VERSION)\"
main.o server.o: CFLAGS += -DDEFAULT_PORT=3000
discid.o: CFLAGS += $(DISCID_CFLAGS)
mpris.o: CFLAGS += $(LIBSYSTEMD_CFLAGS)

.version: Makefile
	@test "`cat $@ 2> /dev/null`" = "$(VERSION)" && exit 0; \
	echo "   GEN    $@"; echo $(VERSION) > $@

# programs {{{
cmus-y := \
	ape.o browser.o buffer.o cache.o channelmap.o cmdline.o cmus.o command_mode.o \
	comment.o convert.lo cue.o cue_utils.o debug.o discid.o editable.o expr.o \
	filters.o format_print.o gbuf.o glob.o help.o history.o http.o id3.o input.o \
	job.o keys.o keyval.o lib.o load_dir.o locking.o mergesort.o misc.o options.o \
	output.o pcm.o player.o play_queue.o pl.o rbtree.o read_wrapper.o search_mode.o \
	search.o server.o spawn.o tabexp_file.o tabexp.o track_info.o track.o tree.o \
	uchar.o u_collate.o ui_curses.o window.o worker.o xstrjoin.o

cmus-$(CONFIG_MPRIS) += mpris.o

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
cdio-objs		:= ip/cdio.lo
flac-objs		:= ip/flac.lo
mad-objs		:= ip/mad.lo ip/nomad.lo
mikmod-objs		:= ip/mikmod.lo
modplug-objs		:= ip/modplug.lo
bass-objs		:= ip/bass.lo
mpc-objs		:= ip/mpc.lo
vorbis-objs		:= ip/vorbis.lo
opus-objs		:= ip/opus.lo
wavpack-objs		:= ip/wavpack.lo
wav-objs		:= ip/wav.lo
mp4-objs		:= ip/mp4.lo
aac-objs		:= ip/aac.lo
ffmpeg-objs		:= ip/ffmpeg.lo
cue-objs		:= ip/cue.lo
vtx-objs		:= ip/vtx.lo

ip-$(CONFIG_CDIO)	+= ip/cdio.so
ip-$(CONFIG_FLAC)	+= ip/flac.so
ip-$(CONFIG_MAD)	+= ip/mad.so
ip-$(CONFIG_MIKMOD)	+= ip/mikmod.so
ip-$(CONFIG_MODPLUG)	+= ip/modplug.so
ip-$(CONFIG_BASS)	+= ip/bass.so
ip-$(CONFIG_MPC)	+= ip/mpc.so
ip-$(CONFIG_VORBIS)	+= ip/vorbis.so
ip-$(CONFIG_OPUS)	+= ip/opus.so
ip-$(CONFIG_WAVPACK)	+= ip/wavpack.so
ip-$(CONFIG_WAV)	+= ip/wav.so
ip-$(CONFIG_MP4)	+= ip/mp4.so
ip-$(CONFIG_AAC)	+= ip/aac.so
ip-$(CONFIG_FFMPEG)	+= ip/ffmpeg.so
ip-$(CONFIG_CUE)	+= ip/cue.so
ip-$(CONFIG_VTX)	+= ip/vtx.so

$(cdio-objs):		CFLAGS += $(CDIO_CFLAGS) $(CDDB_CFLAGS)
$(flac-objs):		CFLAGS += $(FLAC_CFLAGS)
$(mad-objs):		CFLAGS += $(MAD_CFLAGS)
$(mikmod-objs):		CFLAGS += $(MIKMOD_CFLAGS)
$(modplug-objs):	CFLAGS += $(MODPLUG_CFLAGS)
$(bass-objs):		CFLAGS += $(BASS_CFLAGS)
$(mpc-objs):		CFLAGS += $(MPC_CFLAGS)
$(vorbis-objs):		CFLAGS += $(VORBIS_CFLAGS)
$(opus-objs):		CFLAGS += $(OPUS_CFLAGS)
$(wavpack-objs):	CFLAGS += $(WAVPACK_CFLAGS)
$(mp4-objs):		CFLAGS += $(MP4_CFLAGS)
$(aac-objs):		CFLAGS += $(AAC_CFLAGS)
$(ffmpeg-objs):		CFLAGS += $(FFMPEG_CFLAGS)
$(vtx-objs):		CFLAGS += $(VTX_CFLAGS)

ip/cdio.so: $(cdio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(CDIO_LIBS) $(CDDB_LIBS))

ip/flac.so: $(flac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(FLAC_LIBS))

ip/mad.so: $(mad-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MAD_LIBS) $(ICONV_LIBS))

ip/mikmod.so: $(mikmod-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MIKMOD_LIBS))

ip/modplug.so: $(modplug-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MODPLUG_LIBS))

ip/bass.so: $(bass-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(BASS_LIBS))

ip/mpc.so: $(mpc-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MPC_LIBS))

ip/vorbis.so: $(vorbis-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(VORBIS_LIBS))

ip/opus.so: $(opus-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(OPUS_LIBS))

ip/wavpack.so: $(wavpack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(WAVPACK_LIBS))

ip/wav.so: $(wav-objs) $(libcmus-y)
	$(call cmd,ld_dl,)

ip/mp4.so: $(mp4-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(MP4_LIBS))

ip/aac.so: $(aac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(AAC_LIBS))

ip/ffmpeg.so: $(ffmpeg-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(FFMPEG_LIBS))

ip/cue.so: $(cue-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(CUE_LIBS))

ip/vtx.so: $(vtx-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(VTX_LIBS))

# }}}

# output plugins {{{
pulse-objs		:= op/pulse.lo
alsa-objs		:= op/alsa.lo op/mixer_alsa.lo
jack-objs		:= op/jack.lo
arts-objs		:= op/arts.lo
oss-objs		:= op/oss.lo op/mixer_oss.lo
sun-objs		:= op/sun.lo op/mixer_sun.lo
sndio-objs		:= op/sndio.lo
ao-objs			:= op/ao.lo
coreaudio-objs		:= op/coreaudio.lo
waveout-objs		:= op/waveout.lo
roar-objs               := op/roar.lo

op-$(CONFIG_PULSE)	+= op/pulse.so
op-$(CONFIG_ALSA)	+= op/alsa.so
op-$(CONFIG_JACK)	+= op/jack.so
op-$(CONFIG_ARTS)	+= op/arts.so
op-$(CONFIG_OSS)	+= op/oss.so
op-$(CONFIG_SNDIO)	+= op/sndio.so
op-$(CONFIG_SUN)	+= op/sun.so
op-$(CONFIG_COREAUDIO)	+= op/coreaudio.so
op-$(CONFIG_AO)		+= op/ao.so
op-$(CONFIG_WAVEOUT)	+= op/waveout.so
op-$(CONFIG_ROAR)       += op/roar.so

$(pulse-objs): CFLAGS		+= $(PULSE_CFLAGS)
$(alsa-objs): CFLAGS		+= $(ALSA_CFLAGS)
$(jack-objs): CFLAGS		+= $(JACK_CFLAGS) $(SAMPLERATE_CFLAGS)
$(arts-objs): CFLAGS		+= $(ARTS_CFLAGS)
$(oss-objs):  CFLAGS		+= $(OSS_CFLAGS)
$(sndio-objs): CFLAGS		+= $(SNDIO_CFLAGS)
$(sun-objs):  CFLAGS		+= $(SUN_CFLAGS)
$(ao-objs):   CFLAGS		+= $(AO_CFLAGS)
$(coreaudio-objs): CFLAGS	+= $(COREAUDIO_CFLAGS)
$(waveout-objs): CFLAGS 	+= $(WAVEOUT_CFLAGS)
$(roar-objs): CFLAGS		+= $(ROAR_CFLAGS)

op/pulse.so: $(pulse-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(PULSE_LIBS))

op/alsa.so: $(alsa-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ALSA_LIBS))

op/jack.so: $(jack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(JACK_LIBS) $(SAMPLERATE_LIBS))

op/arts.so: $(arts-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ARTS_LIBS))

op/oss.so: $(oss-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(OSS_LIBS))

op/sndio.so: $(sndio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(SNDIO_LIBS))

op/sun.so: $(sun-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(SUN_LIBS))

op/ao.so: $(ao-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(AO_LIBS))

op/coreaudio.so: $(coreaudio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(COREAUDIO_LIBS))

op/waveout.so: $(waveout-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(WAVEOUT_LIBS))

op/roar.so: $(roar-objs) $(libcmus-y)
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

clean		+= *.o ip/*.lo op/*.lo ip/*.so op/*.so *.lo cmus libcmus.a cmus.def cmus.base cmus.exp cmus-remote Doc/*.o Doc/ttman Doc/*.1 Doc/*.7 .install.log
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
