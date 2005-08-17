CFLAGS	+= -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g

objs	:= main.o

cmus-remote: $(objs) $(top_builddir)/common/common.a
	$(call cmd,ld,)

install-exec:
	$(INSTALL) --fmode=0755 $(bindir) cmus-remote

targets-y	+= cmus-remote
clean		+= $(objs)
