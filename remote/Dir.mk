objs	:= main.o

CFLAGS	+= -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g

cmus-remote: $(objs) $(top_builddir)/common/common.a
	$(call cmd,ld,)

install-exec:
	$(INSTALL) --auto cmus-remote

targets	:= cmus-remote
clean	+= $(objs)
