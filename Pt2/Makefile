obj-m += my_timer.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=/usr/src/my_timer modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=/usr/src/my_timer clean

test:
	sudo insmod my_timer.ko
	cat /proc/timed
	sleep 1
	cat /proc/timed
	sleep 3
	cat /proc/timed
	sudo rmmod my_timer
