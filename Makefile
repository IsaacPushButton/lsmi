
LIBS=-lasound
CFLAGS=-g -Wall -pedantic $(LIBS)

.PHONY : clean all doc install

BINS=lsmi-monterey lsmi-joystick lsmi-mouse lsmi-keyhack  lsmi-ps3

all: $(BINS)

clean:
	rm -f $(BINS)

seq.o: seq.c seq.h

sig.o: sig.c

OBJS=seq.o sig.o

lsmi-monterey: lsmi-monterey.c $(OBJS)

lsmi-joystick: lsmi-joystick.c $(OBJS)

lsmi-mouse: lsmi-mouse.c $(OBJS)

lsmi-keyhack: lsmi-keyhack.c $(OBJS)

lsmi-ps3: lsmi-ps3.c $(OBJS)
doc:
	mup html < README.mu > README.html
	mup < README.mu > README

install: $(BINS)
	install $(BINS) /usr/local/bin

