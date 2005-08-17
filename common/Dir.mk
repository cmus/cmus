CFLAGS	+= -I$(srcdir) -g

objs	:= file.o get_option.o path.o xmalloc.o

common.a: $(objs)
	$(call cmd,ar)

targets-y	+= common.a
clean		+= $(objs)
