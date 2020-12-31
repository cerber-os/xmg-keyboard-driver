FILES = $(wildcard *.c)

default: boot brita color dchu timeout

boot: boot.c
	gcc -o boot boot.c
brita: brita.c
	gcc -o brita brita.c
color: color.c
	gcc -o color color.c
dchu: dchu.c
	gcc -o dchu dchu.c
timeout: timeout.c
	gcc -o timeout timeout.c

clean:
	rm -f boot brita color dchu timeout
