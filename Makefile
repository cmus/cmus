REV	= HEAD

# version from an annotated tag
_ver0	= $(shell git describe $(REV) 2>/dev/null)
# version from a plain tag
_ver1	= $(shell git describe --tags $(REV) 2>/dev/null)
# SHA1
_ver2	= $(shell git rev-parse --verify --short $(REV) 2>/dev/null)
# hand-made
_ver3	= v2.12.0

VERSION	= $(or $(_ver0),$(_ver1),$(_ver2),$(_ver3))

all: main plugins man

-include config.mk
include scripts/lib.mk

ifeq ($(STATICPLUGIN),y)
staticplugin := 1
endif

CFLAGS += -D_FILE_OFFSET_BITS=64

CMUS_LIBS = $(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) $(DISCID_LIBS) \
			-lm $(COMPAT_LIBS) $(LIBSYSTEMD_LIBS)

command_mode.o input.o main.o ui_curses.o op/pulse.lo op/pulse.o: .version
command_mode.o input.o main.o ui_curses.o op/pulse.lo op/pulse.o: CFLAGS += -DVERSION=\"$(VERSION)\"
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
	output.o pcm.o player.o play_queue.o pl.o pl_env.o rbtree.o read_wrapper.o \
	search_mode.o search.o server.o spawn.o tabexp_file.o tabexp.o track_info.o \
	track.o tree.o uchar.o u_collate.o ui_curses.o window.o worker.o xstrjoin.o

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
cdio-objs		:= ip/cdio.$(if $(staticplugin),o,lo)
flac-objs		:= ip/flac.$(if $(staticplugin),o,lo)
mad-objs		:= ip/mad.lo ip/nomad.$(if $(staticplugin),o,lo)
mikmod-objs		:= ip/mikmod.$(if $(staticplugin),o,lo)
modplug-objs		:= ip/modplug.$(if $(staticplugin),o,lo)
bass-objs		:= ip/bass.$(if $(staticplugin),o,lo)
mpc-objs		:= ip/mpc.$(if $(staticplugin),o,lo)
vorbis-objs		:= ip/vorbis.$(if $(staticplugin),o,lo)
opus-objs		:= ip/opus.$(if $(staticplugin),o,lo)
wavpack-objs		:= ip/wavpack.$(if $(staticplugin),o,lo)
wav-objs		:= ip/wav.$(if $(staticplugin),o,lo)
mp4-objs		:= ip/mp4.$(if $(staticplugin),o,lo)
aac-objs		:= ip/aac.$(if $(staticplugin),o,lo)
ffmpeg-objs		:= ip/ffmpeg.$(if $(staticplugin),o,lo)
cue-objs		:= ip/cue.$(if $(staticplugin),o,lo)
vtx-objs		:= ip/vtx.$(if $(staticplugin),o,lo)

ip-$(CONFIG_CDIO)	+= $(if $(staticplugin),$(cdio-objs),ip/cdio.so)
ip-$(CONFIG_FLAC)	+= $(if $(staticplugin),$(flac-objs),ip/flac.so)
ip-$(CONFIG_MAD)	+= $(if $(staticplugin),$(mad-objs),ip/mad.so)
ip-$(CONFIG_MIKMOD)	+= $(if $(staticplugin),$(mikmod-objs),ip/mikmod.so)
ip-$(CONFIG_MODPLUG)	+= $(if $(staticplugin),$(modplug-objs),ip/modplug.so)
ip-$(CONFIG_BASS)	+= $(if $(staticplugin),$(bass-objs),ip/bass.so)
ip-$(CONFIG_MPC)	+= $(if $(staticplugin),$(mpc-objs),ip/mpc.so)
ip-$(CONFIG_VORBIS)	+= $(if $(staticplugin),$(vorbis-objs),ip/vorbis.so)
ip-$(CONFIG_OPUS)	+= $(if $(staticplugin),$(opus-objs),ip/opus.so)
ip-$(CONFIG_WAVPACK)	+= $(if $(staticplugin),$(wavpack-objs),ip/wavpack.so)
ip-$(CONFIG_WAV)	+= $(if $(staticplugin),$(wav-objs),ip/wav.so)
ip-$(CONFIG_MP4)	+= $(if $(staticplugin),$(mp4-objs),ip/mp4.so)
ip-$(CONFIG_AAC)	+= $(if $(staticplugin),$(aac-objs),ip/aac.so)
ip-$(CONFIG_FFMPEG)	+= $(if $(staticplugin),$(ffmpeg-objs),ip/ffmpeg.so)
ip-$(CONFIG_CUE)	+= $(if $(staticplugin),$(cue-objs),ip/cue.so)
ip-$(CONFIG_VTX)	+= $(if $(staticplugin),$(vtx-objs),ip/vtx.so)

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

$(if $(staticplugin),ip-$(CONFIG_CDIO),cdio)-libs		+= $(CDIO_LIBS) $(CDDB_LIBS)
$(if $(staticplugin),ip-$(CONFIG_FLAC),flac)-libs		+= $(FLAC_LIBS)
$(if $(staticplugin),ip-$(CONFIG_MAD),mad)-libs		+= $(MAD_LIBS) $(ICONV_LIBS)
$(if $(staticplugin),ip-$(CONFIG_MIKMOD),mikmod)-libs	+= $(MIKMOD_LIBS)
$(if $(staticplugin),ip-$(CONFIG_MODPLUG),modplug)-libs	+= $(MODPLUG_LIBS)
$(if $(staticplugin),ip-$(CONFIG_BASS),bass)-libs		+= $(BASS_LIBS)
$(if $(staticplugin),ip-$(CONFIG_MPC),mpc)-libs		+= $(MPC_LIBS)
$(if $(staticplugin),ip-$(CONFIG_VORBIS),vorbis)-libs	+= $(VORBIS_LIBS)
$(if $(staticplugin),ip-$(CONFIG_OPUS),opus)-libs		+= $(OPUS_LIBS)
$(if $(staticplugin),ip-$(CONFIG_WAVPACK),wavpack)-libs	+= $(WAVPACK_LIBS)
$(if $(staticplugin),ip-$(CONFIG_MP4),mp4)-libs		+= $(MP4_LIBS)
$(if $(staticplugin),ip-$(CONFIG_AAC),aac)-libs		+= $(AAC_LIBS)
$(if $(staticplugin),ip-$(CONFIG_FFMPEG),ffmpeg)-libs	+= $(FFMPEG_LIBS)
$(if $(staticplugin),ip-$(CONFIG_VTX),cue)-libs		+= -lm
$(if $(staticplugin),ip-$(CONFIG_VTX),vtx)-libs		+= $(VTX_LIBS)

ifdef staticplugin
cmus: $(ip-y)
CMUS_LIBS += $(ip-y-libs)
endif

ip/cdio.so: $(cdio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(cdio-libs))

ip/flac.so: $(flac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(flac-libs))

ip/mad.so: $(mad-objs) $(libcmus-y)
	$(call cmd,ld_dl,-lm $(mad-libs))

ip/mikmod.so: $(mikmod-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(mikmod-libs))

ip/modplug.so: $(modplug-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(modplug-libs))

ip/bass.so: $(bass-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(bass-libs))

ip/mpc.so: $(mpc-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(mpc-libs))

ip/vorbis.so: $(vorbis-objs) $(libcmus-y)
	$(call cmd,ld_dl,-lm $(vorbis-libs))

ip/opus.so: $(opus-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(opus-libs))

ip/wavpack.so: $(wavpack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(wavpack-libs))

ip/wav.so: $(wav-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(wav-libs))

ip/mp4.so: $(mp4-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(mp4-libs))

ip/aac.so: $(aac-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(aac-libs))

ip/ffmpeg.so: $(ffmpeg-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ffmpeg-libs))

ip/cue.so: $(cue-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(cue-libs))

ip/vtx.so: $(vtx-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(vtx-libs))

# }}}

# output plugins {{{
pulse-objs		:= op/pulse.$(if $(staticplugin),o,lo)
alsa-objs		:= op/alsa.$(if $(staticplugin),o,lo)
jack-objs		:= op/jack.$(if $(staticplugin),o,lo)
arts-objs		:= op/arts.$(if $(staticplugin),o,lo)
oss-objs		:= op/oss.$(if $(staticplugin),o,lo)
sun-objs		:= op/sun.$(if $(staticplugin),o,lo)
sndio-objs		:= op/sndio.$(if $(staticplugin),o,lo)
ao-objs			:= op/ao.$(if $(staticplugin),o,lo)
coreaudio-objs		:= op/coreaudio.$(if $(staticplugin),o,lo)
waveout-objs		:= op/waveout.$(if $(staticplugin),o,lo)
roar-objs               := op/roar.$(if $(staticplugin),o,lo)
aaudio-objs		:= op/aaudio.$(if $(staticplugin),o,lo)

op-$(CONFIG_PULSE)	+= $(if $(staticplugin),$(pulse-objs),op/pulse.so)
op-$(CONFIG_ALSA)	+= $(if $(staticplugin),$(alsa-objs),op/alsa.so)
op-$(CONFIG_JACK)	+= $(if $(staticplugin),$(jack-objs),op/jack.so)
op-$(CONFIG_ARTS)	+= $(if $(staticplugin),$(arts-objs),op/arts.so)
op-$(CONFIG_OSS)	+= $(if $(staticplugin),$(oss-objs),op/oss.so)
op-$(CONFIG_SNDIO)	+= $(if $(staticplugin),$(sndio-objs),op/sndio.so)
op-$(CONFIG_SUN)	+= $(if $(staticplugin),$(sun-objs),op/sun.so)
op-$(CONFIG_COREAUDIO)	+= $(if $(staticplugin),$(coreaudio-objs),op/coreaudio.so)
op-$(CONFIG_AO)		+= $(if $(staticplugin),$(ao-objs),op/ao.so)
op-$(CONFIG_WAVEOUT)	+= $(if $(staticplugin),$(waveout-objs),op/waveout.so)
op-$(CONFIG_ROAR)       += $(if $(staticplugin),$(roar-objs),op/roar.so)
op-$(CONFIG_AAUDIO)	+= $(if $(staticplugin),$(aaudio-objs),op/aaudio.so)

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
$(aaudio-objs): CFLAGS		+= $(AAUDIO_CFLAGS)

$(if $(staticplugin),op-$(CONFIG_PULSE),pulse)-libs		+= $(PULSE_LIBS)
$(if $(staticplugin),op-$(CONFIG_ALSA),alsa)-libs			+= $(ALSA_LIBS)
$(if $(staticplugin),op-$(CONFIG_JACK),jack)-libs			+= $(JACK_LIBS) $(SAMPLERATE_LIBS)
$(if $(staticplugin),op-$(CONFIG_ARTS),arts)-libs			+= $(ARTS_LIBS)
$(if $(staticplugin),op-$(CONFIG_OSS),oss)-libs			+= $(OSS_LIBS)
$(if $(staticplugin),op-$(CONFIG_SNDIO),sndio)-libs		+= $(SNDIO_LIBS)
$(if $(staticplugin),op-$(CONFIG_SUN),sun)-libs			+= $(SUN_LIBS)
$(if $(staticplugin),op-$(CONFIG_AO),ao)-libs			+= $(AO_LIBS)
$(if $(staticplugin),op-$(CONFIG_COREAUDIO),coreaudio)-libs	+= $(COREAUDIO_LIBS)
$(if $(staticplugin),op-$(CONFIG_WAVEOUT),waveout)-libs		+= $(WAVEOUT_LIBS)
$(if $(staticplugin),op-$(CONFIG_ROAR),roar)-libs			+= $(ROAR_LIBS)
$(if $(staticplugin),op-$(CONFIG_AAUDIO),aaudio)-libs		+= $(AAUDIO_LIBS)

ifdef staticplugin
cmus: $(op-y)
CMUS_LIBS += $(op-y-libs)
endif

op/pulse.so: $(pulse-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(pulse-libs))

op/alsa.so: $(alsa-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(alsa-libs))

op/jack.so: $(jack-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(jack-libs))

op/arts.so: $(arts-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(arts-libs))

op/oss.so: $(oss-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(oss-libs))

op/sndio.so: $(sndio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(sndio-libs))

op/sun.so: $(sun-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(sun-libs))

op/ao.so: $(ao-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(ao-libs))

op/coreaudio.so: $(coreaudio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(coreaudio-libs))

op/waveout.so: $(waveout-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(waveout-libs))

op/roar.so: $(roar-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(roar-libs))

op/aaudio.so: $(aaudio-objs) $(libcmus-y)
	$(call cmd,ld_dl,$(aaudio-libs))
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

clean		+= *.o ip/*.lo ip/*.o op/*.lo op/*.o ip/*.so op/*.so *.lo cmus libcmus.a cmus.def cmus.base cmus.exp cmus-remote Doc/*.o Doc/ttman Doc/*.1 Doc/*.7 .install.log
distclean	+= .version config.mk config/*.h tags

main: cmus cmus-remote
plugins: $(ip-y) $(op-y)
man: $(man1) $(man7)

install-main: main
	$(INSTALL) -m755 $(bindir) cmus cmus-remote

install-plugins: plugins
ifndef staticplugin
	$(INSTALL) -m755 $(libdir)/cmus/ip $(ip-y)
	$(INSTALL) -m755 $(libdir)/cmus/op $(op-y)
endif

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
