#include <stdio.h>
#include <unistd.h>
#include <malloc.h>

#include "do_event.h"

/***
gcc -I /usr/local/glib/include/glib-2.0 -I/usr/local/glib/lib/glib-2.0/include/  -I/usr/local/libevent/include    -L/usr/local/glib/lib -L/usr/local/libevent/lib  -o server do_event.c do_event.h main.c  -std=gnu11 -lgthread-2.0 -lglib-2.0 -levent -g
****/
int main(int argc, char** argv)
{
	init_lacs_server();
	while(1){
		sleep(10);
	}
	return 0;
}