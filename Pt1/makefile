default : part1.x empty.x
	strace -o part1.trace ./part1.x
	strace -o empty.trace ./empty.x
	chmod 700 *

part1.x : part1.c
	gcc -o part1.x part1.c

empty.x : empty.c
	gcc -o empty.x empty.c

clean :
	rm -f *.trace
	rm -f *.x
