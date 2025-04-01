# machine: a renderer for Matt Keeter's Prospero Challenge
# Simeon Veldstra, 2025
#


all: machine

machine: 
	gcc machine.c -o machine -lm -Ofast -Wall 

purge: clean
	rm -f machine

clean:
	rm -f *.o
