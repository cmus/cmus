CFLAGS	+= -I$(srcdir) -g

common-objs-y	:= file.o get_option.o path.o xmalloc.o
archives-y	+= common
