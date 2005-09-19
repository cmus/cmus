CFLAGS	+= -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g

objs	:= main.o

cmus-remote: $(objs) $(top_builddir)/common/common.a
	$(call cmd,ld,)

exec-y	+= cmus-remote
clean	+= $(objs)
