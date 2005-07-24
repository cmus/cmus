alsa-objs-y		:= alsa.lo mixer_alsa.lo
arts-objs-y		:= arts.lo
oss-objs-y		:= oss.lo mixer_oss.lo

alsa-libs		:= $(ALSA_LIBS)
arts-libs		:= $(ARTS_LIBS)

CFLAGS			+= -I$(srcdir) -I$(top_srcdir)/cmus -I$(top_srcdir)/common

$(alsa-objs-y): CFLAGS	+= $(ALSA_CFLAGS)
$(arts-objs-y): CFLAGS	+= $(ARTS_CFLAGS)

libs-$(CONFIG_ALSA)	+= alsa
libs-$(CONFIG_ARTS)	+= arts
libs-$(CONFIG_OSS)	+= oss

plugins			:= $(addsuffix .so,$(libs-y))

install-exec:
	$(INSTALL) --fmode=0755 $(pkglibdir)/op $(plugins)
