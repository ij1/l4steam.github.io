CFLAGS=-Wall

all: coalescer forwarder

coalescer: coalescer.o common.o strategy.o

forwarder: forwarder.o common.o

common.o: common.c common.h
strategy.o: strategy.c coalescer.h
coalescer.o: coalescer.c common.h coalescer.h
forwarder.o: forwarder.c common.h

clean:
	rm -f coalescer.o forwarder.o common.o strategy.o coalescer forwarder
