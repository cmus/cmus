alsa-objs		:= alsa.lo mixer_alsa.lo
arts-objs		:= arts.lo
oss-objs		:= oss.lo mixer_oss.lo

targets-$(CONFIG_ALSA)	+= alsa.so
targets-$(CONFIG_ARTS)	+= arts.so
targets-$(CONFIG_OSS)	+= oss.so

clean += $(alsa-objs) $(arts-objs) $(oss-objs)

CFLAGS			+= -I$(srcdir) -I$(top_srcdir)/cmus -I$(top_srcdir)/common

$(alsa-objs): CFLAGS	+= $(ALSA_CFLAGS)
$(arts-objs): CFLAGS	+= $(ARTS_CFLAGS)

alsa.so: $(alsa-objs)
	$(call cmd,ld_so,$(ALSA_LIBS))

arts.so: $(arts-objs)
	$(call cmd,ld_so,$(ARTS_LIBS))

oss.so: $(oss-objs)
	$(call cmd,ld_so,)

install-exec:
	$(INSTALL) --fmode=0755 $(pkglibdir)/op $(targets-y)
