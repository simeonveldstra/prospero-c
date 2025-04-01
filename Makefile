# machine: a renderer for Matt Keeter's Prospero Challenge
# Simeon Veldstra, 2025
#

SOURCES=machine.c

CC=gcc
CFLAGS=-Ofast -Wall
LFLAGS=-lm


OBJSC=$(SOURCES:.c=.o)
all: $(SOURCES) machine

machine: $(OBJSC) 
	gcc machine.c $(LFLAGS) $(CFLAGS) $(OBJS) -o machine  

purge: clean
	rm -f machine

clean:
	rm -f *.o
