
all: clean libocheck.so

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

libocheck_OBJS:= \
	lib/ocheck.o \
	lib/allocs.o \
	lib/file.o

libocheck.so: $(libocheck_OBJS)
	$(CC) -shared $(LDFLAGS) $(CFLAGS) -o $@ $^ -ldl

clean:
	rm -f *.so $(libocheck_OBJS)
