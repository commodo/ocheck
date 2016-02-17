
all: clean libocheck.so ocheckd

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

ocheckd_OBJS:= \
	ocheckd.o

libocheck_OBJS:= \
	lib/ocheck.o \
	lib/backtraces.o \
	lib/allocs.o

libocheck.so: $(libocheck_OBJS)
	$(CC) -shared $(LDFLAGS) $(CFLAGS) -o $@ $^ -ldl

ocheckd: $(ocheckd_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^ -lubox -lubus

clean:
	rm -f *.so $(libocheck_OBJS)
	rm -f ocheckd $(ocheckd_OBJS)

