CFLAGS	+= -I$(top_builddir) -I$(top_srcdir)/common -I$(srcdir) -g

cmus-remote-objs-y	:= main.o
cmus-remote-libs	:= $(top_builddir)/common/common.a

bin-programs-y		+= cmus-remote
