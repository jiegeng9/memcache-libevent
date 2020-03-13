#ifndef DO_EVENT_H_
#define DO_EVENT_H_
#include <glib.h>

#include <event2/bufferevent.h>
#include <sys/time.h>

typedef struct {
    int t_id;
    struct event_base* base;              //线程所使用的event_base
    struct event* notify_event;           //用于监听管道读事件的event
    int notify_receive_fd;                //管道的读端fd
    int notify_send_fd;                   //管道的写端fd
    GAsyncQueue* new_conn_queue;
 
} LIBEVENT_THREAD;

typedef struct {
    struct bufferevent* bufferevent;
    int t_id;                                //第几个LIBEVENT_THREAD
    int fd;                                  //sp连接的socket
}CLIENT;

void init_lacs_server();

#endif
