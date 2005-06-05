obj-y := file.o get_option.o path.o xmalloc.o

CFLAGS += -I$(srcdir) -g

common.a: $(obj-y)
	$(call cmd,ar)

targets	:= common.a
