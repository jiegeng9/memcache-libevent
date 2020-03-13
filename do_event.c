#include <arpa/inet.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/util.h>

#include "do_event.h"

#define SERVER_PORT 8888

static LIBEVENT_THREAD* threads;
static int last_thread = 0;
int work_thread_num = 3;
gint start = 0;


gpointer start_work_thread(gpointer arg);
gpointer start_server_thread(gpointer arg);

//管道可读回调
static void on_pipe_read(int fd, short which, void* arg);
static void on_accept(struct evconnlistener* listener, evutil_socket_t fd, struct sockaddr* addr, int socklen, void* arg);

static void on_read(struct bufferevent* bev, void* arg);
static void on_write(struct bufferevent* bev, void* arg);
static void on_event(struct bufferevent* bev, short events, void* arg);


void init_lacs_server() {
    threads = calloc(work_thread_num, sizeof(LIBEVENT_THREAD));
    if (!threads) {
        printf("server can't allocate thread descriptors\n");
    }
    for (int i = 0; i < work_thread_num; i++) {
        threads[i].t_id = i;
        g_thread_create(start_work_thread, (void*)&threads[i], FALSE, NULL);
    }
    while (start != work_thread_num)
    {
        g_usleep(1000);
        printf("server start:%d\n", start);
    }
    g_thread_create(start_server_thread, NULL, FALSE, NULL);
    return;
}

gpointer start_server_thread(gpointer arg) {
    struct event_base* base = event_base_new();
    if (!base) {
        printf("could not initialize libevent!\n");
        return NULL;
    }
    evthread_make_base_notifiable(base);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_family = AF_INET;

    struct evconnlistener* conn_listen = evconnlistener_new_bind(base, on_accept, base, LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, 20, (struct sockaddr*) & addr, sizeof(addr));
    if (NULL == conn_listen)
    {
        event_base_free(base);
        printf("evconnlistener_new_bind failed,please check listen port\n");
        exit(-1);
        return NULL;
    }

    printf("server start succeed\n");
    event_base_dispatch(base);
    evconnlistener_free(conn_listen);
    event_base_free(base);
    return NULL;
}

void disp_work_thread(CLIENT* client) {
    if (client == NULL) {
        return;
    }
    last_thread = (last_thread + 1) % work_thread_num;
    LIBEVENT_THREAD* me = threads + last_thread;
    client->t_id = last_thread;

    char buf[1];
    buf[0] = 0;
    g_async_queue_push(me->new_conn_queue, client);
    printf("dispatch work thread :%d,notify_send_fd :%d\n", last_thread, me->notify_send_fd);
    if (write(me->notify_send_fd, buf, 1) == -1) {
        printf("write pipe error\n");
    }
    return;
}

gpointer start_work_thread(gpointer arg) {
    int pipe_fd[2];
    LIBEVENT_THREAD* me = (LIBEVENT_THREAD*)arg;

    struct timeval tv;
    me->new_conn_queue = g_async_queue_new();
    me->base = event_base_new();
    if (!me->base) {
        printf("can not allocate event_base\n");
        return NULL;
    }

    if (pipe(pipe_fd)) {
        printf("can not create notify pipe\n");
    }

    me->notify_receive_fd = pipe_fd[0];
    me->notify_send_fd = pipe_fd[1];
    //缺少这个EV_PERSIST，如果管道没有read，事件会一直触发
    //缺少这个EV_PERSIST，在管道有read情况下，循环一次dispatch work之后，管道事件不能再响应
    me->notify_event = event_new(me->base, me->notify_receive_fd, EV_READ | EV_PERSIST, on_pipe_read, me);
    if (event_add(me->notify_event, NULL) == -1) {
        printf("Can't monitor libevent notify pipe\n");
    }

    g_atomic_int_inc(&start);
    printf("init one workd thread succeed\n");
    event_base_dispatch(me->base);
    event_base_free(me->base);
    return NULL;
}

/**
   A callback that we invoke when a listener has a new connection.

   @param listener The evconnlistener
   @param fd The new file descriptor
   @param addr The source address of the connection
   @param socklen The length of addr
   @param user_arg the pointer passed to evconnlistener_new()
 */

static void on_accept(struct evconnlistener* listener, evutil_socket_t fd, struct sockaddr* addr, int socklen, void* arg) {

    printf("client connect succeed,ip=%s,fd=%d\n", addr->sa_data, fd);
    struct event_base* base = (struct event_base*)arg;

    CLIENT* client = (CLIENT*)malloc(sizeof(CLIENT));
    memset(client, 0, sizeof(CLIENT));
    client->fd = fd;
    disp_work_thread(client);
    return;
}


static void on_pipe_read(int fd, short which, void* arg) {

    LIBEVENT_THREAD* me = (LIBEVENT_THREAD*)arg;
	CLIENT* client = NULL;
    guchar buf[1];
    if (!arg) {
        return;
    }
   
    read(me->notify_receive_fd, buf, 1);
    client = g_async_queue_try_pop(me->new_conn_queue);
    evutil_make_socket_nonblocking(client->fd);

    //BEV_OPT_THREADSAFE bufferevent不会涉及多线程访问
    struct bufferevent* bev = bufferevent_socket_new(me->base, client->fd, BEV_OPT_CLOSE_ON_FREE);
    if (NULL == bev)
    {
        printf("bufferevent_socket_new Error!\n");
        return;
    }

    bufferevent_setcb(bev, on_read, on_write, on_event, client);
    bufferevent_enable(bev, EV_READ);
    client->bufferevent = bev;
    printf("pipe have active , the sp socket fd :%d\n", client->fd);
    return;
}

static void on_read(struct bufferevent* bev, void* arg) {
    CLIENT* client = (CLIENT*)arg;
	guchar buffer[1024] = { 0 };
	bufferevent_read(client->bufferevent,buffer,sizeof(buffer));
	printf("recieve client data :%s\n",buffer);
	bufferevent_write(client->bufferevent,buffer,strlen(buffer));
    return;
}

static void on_write(struct bufferevent* bev, void* arg) {
	CLIENT* client = (CLIENT*)arg;
    printf("close socket,which fd=%d", client->fd);
    bufferevent_free(bev);
    free(client);
    return;
}

static void on_event(struct bufferevent* bev, short what, void* arg) {
    printf("on_event(),what:%d",what);
    if (!arg) {
        return;
    }
    CLIENT* client = (CLIENT*)arg;
    LIBEVENT_THREAD* me = threads + (client->t_id);
    //当客户端主动关闭连接。
    if (what & BEV_EVENT_ERROR) {
        printf("client haved close ,the fd :%d \n", bufferevent_getfd(bev));

        if (client->bufferevent != NULL) {
            bufferevent_free(client->bufferevent);
            client->bufferevent = NULL;
        }
        free(client);
    }
    else if(what & BEV_EVENT_EOF)
    {
        bufferevent_free(bev);
    }
    return;
}
