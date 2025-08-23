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
ip-ext := $(if $(staticplugin),o,so)

ip-$(CONFIG_CDIO)	+= ip/cdio.$(ip-ext)
ip-$(CONFIG_FLAC)	+= ip/flac.$(ip-ext)
ip-$(CONFIG_MAD)	+= ip/mad.$(ip-ext)
ip-$(CONFIG_MIKMOD)	+= ip/mikmod.$(ip-ext)
ip-$(CONFIG_MODPLUG)	+= ip/modplug.$(ip-ext)
ip-$(CONFIG_BASS)	+= ip/bass.$(ip-ext)
ip-$(CONFIG_MPC)	+= ip/mpc.$(ip-ext)
ip-$(CONFIG_VORBIS)	+= ip/vorbis.$(ip-ext)
ip-$(CONFIG_OPUS)	+= ip/opus.$(ip-ext)
ip-$(CONFIG_WAVPACK)	+= ip/wavpack.$(ip-ext)
ip-$(CONFIG_WAV)	+= ip/wav.$(ip-ext)
ip-$(CONFIG_MP4)	+= ip/mp4.$(ip-ext)
ip-$(CONFIG_AAC)	+= ip/aac.$(ip-ext)
ip-$(CONFIG_FFMPEG)	+= ip/ffmpeg.$(ip-ext)
ip-$(CONFIG_CUE)	+= ip/cue.$(ip-ext)
ip-$(CONFIG_VTX)	+= ip/vtx.$(ip-ext)

ip/cdio.$(ip-ext):	CFLAGS += $(CDIO_CFLAGS) $(CDDB_CFLAGS)
ip/flac.$(ip-ext):	CFLAGS += $(FLAC_CFLAGS)
ip/mad.$(ip-ext):	CFLAGS += $(MAD_CFLAGS)
ip/mikmod.$(ip-ext):	CFLAGS += $(MIKMOD_CFLAGS)
ip/modplug.$(ip-ext):	CFLAGS += $(MODPLUG_CFLAGS)
ip/bass.$(ip-ext):	CFLAGS += $(BASS_CFLAGS)
ip/mpc.$(ip-ext):	CFLAGS += $(MPC_CFLAGS)
ip/vorbis.$(ip-ext):	CFLAGS += $(VORBIS_CFLAGS)
ip/opus.$(ip-ext):	CFLAGS += $(OPUS_CFLAGS)
ip/wavpack.$(ip-ext):	CFLAGS += $(WAVPACK_CFLAGS)
ip/mp4.$(ip-ext):	CFLAGS += $(MP4_CFLAGS)
ip/aac.$(ip-ext):	CFLAGS += $(AAC_CFLAGS)
ip/ffmpeg.$(ip-ext):	CFLAGS += $(FFMPEG_CFLAGS)
ip/vtx.$(ip-ext):	CFLAGS += $(VTX_CFLAGS)

ip-$(if $(staticplugin),$(CONFIG_CDIO),cdio)-libs	+= $(CDIO_LIBS) $(CDDB_LIBS)
ip-$(if $(staticplugin),$(CONFIG_FLAC),flac)-libs	+= $(FLAC_LIBS)
ip-$(if $(staticplugin),$(CONFIG_MAD),mad)-libs		+= $(MAD_LIBS) $(ICONV_LIBS)
ip-$(if $(staticplugin),$(CONFIG_MIKMOD),mikmod)-libs	+= $(MIKMOD_LIBS)
ip-$(if $(staticplugin),$(CONFIG_MODPLUG),modplug)-libs	+= $(MODPLUG_LIBS)
ip-$(if $(staticplugin),$(CONFIG_BASS),bass)-libs	+= $(BASS_LIBS)
ip-$(if $(staticplugin),$(CONFIG_MPC),mpc)-libs		+= $(MPC_LIBS)
ip-$(if $(staticplugin),$(CONFIG_VORBIS),vorbis)-libs	+= $(VORBIS_LIBS)
ip-$(if $(staticplugin),$(CONFIG_OPUS),opus)-libs	+= $(OPUS_LIBS)
ip-$(if $(staticplugin),$(CONFIG_WAVPACK),wavpack)-libs	+= $(WAVPACK_LIBS)
ip-$(if $(staticplugin),$(CONFIG_MP4),mp4)-libs		+= $(MP4_LIBS)
ip-$(if $(staticplugin),$(CONFIG_AAC),aac)-libs		+= $(AAC_LIBS)
ip-$(if $(staticplugin),$(CONFIG_FFMPEG),ffmpeg)-libs	+= $(FFMPEG_LIBS)
ip-$(if $(staticplugin),$(CONFIG_VTX),cue)-libs		+= -lm
ip-$(if $(staticplugin),$(CONFIG_VTX),vtx)-libs		+= $(VTX_LIBS)

ifdef staticplugin
ip-$(CONFIG_MAD) += ip/nomad.o
else
ip/mad.so: ip/nomad.lo
endif
ip/nomad.$(ip-ext): CFLAGS += $(MAD_CFLAGS)

ifdef staticplugin
cmus: $(ip-y)
CMUS_LIBS += $(ip-y-libs)
else
ip/%.so: ip/%.lo $(libcmus-y)
	$(call cmd,ld_dl,$($(patsubst ip/%.so,ip-%-libs,$@)))
endif

# }}}

# output plugins {{{
op-ext := $(if $(staticplugin),o,so)

op-$(CONFIG_PULSE)	+= op/pulse.$(op-ext)
op-$(CONFIG_ALSA)	+= op/alsa.$(op-ext)
op-$(CONFIG_JACK)	+= op/jack.$(op-ext)
op-$(CONFIG_ARTS)	+= op/arts.$(op-ext)
op-$(CONFIG_OSS)	+= op/oss.$(op-ext)
op-$(CONFIG_SNDIO)	+= op/sndio.$(op-ext)
op-$(CONFIG_SUN)	+= op/sun.$(op-ext)
op-$(CONFIG_COREAUDIO)	+= op/coreaudio.$(op-ext)
op-$(CONFIG_AO)		+= op/ao.$(op-ext)
op-$(CONFIG_WAVEOUT)	+= op/waveout.$(op-ext)
op-$(CONFIG_ROAR)       += op/roar.$(op-ext)
op-$(CONFIG_AAUDIO)	+= op/aaudio.$(op-ext)

$(pulse-objs):		CFLAGS += $(PULSE_CFLAGS)
$(alsa-objs):		CFLAGS += $(ALSA_CFLAGS)
$(jack-objs):		CFLAGS += $(JACK_CFLAGS) $(SAMPLERATE_CFLAGS)
$(arts-objs):		CFLAGS += $(ARTS_CFLAGS)
$(oss-objs):		CFLAGS += $(OSS_CFLAGS)
$(sndio-objs):		CFLAGS += $(SNDIO_CFLAGS)
$(sun-objs):		CFLAGS += $(SUN_CFLAGS)
$(ao-objs):		CFLAGS += $(AO_CFLAGS)
$(coreaudio-objs):	CFLAGS += $(COREAUDIO_CFLAGS)
$(waveout-objs):	CFLAGS += $(WAVEOUT_CFLAGS)
$(roar-objs):		CFLAGS += $(ROAR_CFLAGS)
$(aaudio-objs):		CFLAGS += $(AAUDIO_CFLAGS)

op-$(if $(staticplugin),$(CONFIG_PULSE),pulse)-libs		+= $(PULSE_LIBS)
op-$(if $(staticplugin),$(CONFIG_ALSA),alsa)-libs		+= $(ALSA_LIBS)
op-$(if $(staticplugin),$(CONFIG_JACK),jack)-libs		+= $(JACK_LIBS) $(SAMPLERATE_LIBS)
op-$(if $(staticplugin),$(CONFIG_ARTS),arts)-libs		+= $(ARTS_LIBS)
op-$(if $(staticplugin),$(CONFIG_OSS),oss)-libs			+= $(OSS_LIBS)
op-$(if $(staticplugin),$(CONFIG_SNDIO),sndio)-libs		+= $(SNDIO_LIBS)
op-$(if $(staticplugin),$(CONFIG_SUN),sun)-libs			+= $(SUN_LIBS)
op-$(if $(staticplugin),$(CONFIG_AO),ao)-libs			+= $(AO_LIBS)
op-$(if $(staticplugin),$(CONFIG_COREAUDIO),coreaudio)-libs	+= $(COREAUDIO_LIBS)
op-$(if $(staticplugin),$(CONFIG_WAVEOUT),waveout)-libs		+= $(WAVEOUT_LIBS)
op-$(if $(staticplugin),$(CONFIG_ROAR),roar)-libs		+= $(ROAR_LIBS)
op-$(if $(staticplugin),$(CONFIG_AAUDIO),aaudio)-libs		+= $(AAUDIO_LIBS)

ifdef staticplugin
cmus: $(op-y)
CMUS_LIBS += $(op-y-libs)
else
op/%.so: op/%.lo $(libcmus-y)
	$(call cmd,ld_dl,$($(patsubst op/%.so,op-%-libs,$@)))
endif

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

.PHONY: all main plugins man
.PHONY: install install-main install-plugins install-man
