CFLAGS=-I/usr/include/libxml2 
LIBS=-lcurl -lxml2
OBJS=check_tycon.o

check_tycon: $(OBJS)
	$(CC) $(LIBS) $(OBJS) -o check_tycon

clean:
	rm -f $(OBJS) check_tycon
