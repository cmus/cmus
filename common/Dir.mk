objs	:= file.o get_option.o path.o xmalloc.o

CFLAGS	+= -I$(srcdir) -g

common.a: $(objs)
	$(call cmd,ar)

targets	:= common.a
clean	+= $(objs)
