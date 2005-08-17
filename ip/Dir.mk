flac-objs		:= flac.lo
mad-objs		:= id3.lo mad.lo nomad.lo utf8_encode.lo
modplug-objs		:= modplug.lo
vorbis-objs		:= vorbis.lo
wav-objs		:= wav.lo

targets-$(CONFIG_FLAC)	+= flac.so
targets-$(CONFIG_MAD)	+= mad.so
targets-$(CONFIG_MODPLUG) += modplug.so
targets-$(CONFIG_VORBIS) += vorbis.so
targets-$(CONFIG_WAV)	+= wav.so

clean += $(flac-objs) $(mad-objs) $(modplug-objs) $(vorbis-objs) $(wav-objs)

CFLAGS			+= -I$(srcdir) -I$(top_srcdir)/cmus -I$(top_srcdir)/common

$(flac-objs):		CFLAGS += $(FLAC_CFLAGS)
$(mad-objs):		CFLAGS += $(MAD_CFLAGS)
$(modplug-objs):	CFLAGS += $(MODPLUG_CFLAGS)
$(vorbis-objs):		CFLAGS += $(VORBIS_CFLAGS)

flac.so: $(flac-objs)
	$(call cmd,ld_so,$(FLAC_LIBS))

mad.so: $(mad-objs)
	$(call cmd,ld_so,$(MAD_LIBS))

modplug.so: $(modplug-objs)
	$(call cmd,ld_so,$(MODPLUG_LIBS))

vorbis.so: $(vorbis-objs)
	$(call cmd,ld_so,$(VORBIS_LIBS))

wav.so: $(wav-objs)
	$(call cmd,ld_so,)

install-exec:
	$(INSTALL) --fmode=0755 $(pkglibdir)/ip $(targets-y)
