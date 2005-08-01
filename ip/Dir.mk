flac-objs-y		:= flac.lo
mad-objs-y		:= id3.lo mad.lo nomad.lo utf8_encode.lo
modplug-objs-y		:= modplug.lo
vorbis-objs-y		:= vorbis.lo
wav-objs-y		:= wav.lo

flac-libs		:= $(FLAC_LIBS)
mad-libs		:= $(MAD_LIBS)
modplug-libs		:= $(MODPLUG_LIBS)
vorbis-libs		:= $(VORBIS_LIBS)

libs-$(CONFIG_FLAC)	+= flac
libs-$(CONFIG_MAD)	+= mad
libs-$(CONFIG_MODPLUG)	+= modplug
libs-$(CONFIG_VORBIS)	+= vorbis
libs-$(CONFIG_WAV)	+= wav

CFLAGS			+= -I$(srcdir) -I$(top_srcdir)/cmus -I$(top_srcdir)/common

$(flac-objs-y):		CFLAGS	+= $(FLAC_CFLAGS)
$(mad-objs-y):		CFLAGS	+= $(MAD_CFLAGS)
$(modplug-objs-y):	CFLAGS	+= $(MODPLUG_CFLAGS)
$(vorbis-objs-y):	CFLAGS	+= $(VORBIS_CFLAGS)

plugins			:= $(addsuffix .so,$(libs-y))

install-exec:
	$(INSTALL) --fmode=0755 $(pkglibdir)/ip $(plugins)
