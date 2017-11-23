
CC=gcc
DFLAGS=-ggdb -g -fno-omit-frame-pointer
CFLAGS=-shared -fPIC -std=gnu11 -O2 -Wall $(DFLAGS)
LDFLAGS=-ldl -pthread $(DFLAGS)

default: seqmalloc.so

seqmalloc.so: seqmalloc.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o seqmalloc.so seqmalloc.c

clean:
	rm -f *.so
