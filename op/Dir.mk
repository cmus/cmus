alsa-objs		:= alsa.lo mixer_alsa.lo
arts-objs		:= arts.lo
oss-objs		:= oss.lo mixer_oss.lo

CFLAGS			+= -I$(srcdir) -I$(top_srcdir)/cmus -I$(top_srcdir)/common

$(alsa-objs): CFLAGS	+= $(ALSA_CFLAGS)
$(arts-objs): CFLAGS	+= $(ARTS_CFLAGS)

so-y			:=
so-n			:=
so-$(CONFIG_ALSA)	+= alsa.so
so-$(CONFIG_ARTS)	+= arts.so
so-$(CONFIG_OSS)	+= oss.so

clean-files	:= $(alsa-objs) $(arts-objs) $(oss-objs)
clobber-files	:= $(so-n)
targets		:= $(so-y)

alsa.so: $(alsa-objs)
	$(call cmd,ld_so,$(ALSA_LIBS))

arts.so: $(arts-objs)
	$(call cmd,ld_so,$(ARTS_LIBS))

oss.so: $(oss-objs)
	$(call cmd,ld_so,)

install-exec:
	$(INSTALL) --fmode=0755 $(pkglibdir)/op $(so-y)
