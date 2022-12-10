#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define OK  0
#define NG  1

#define ME              "169.254.4.244"

#define MAX_EVENTS      10
#define MAX_QUEUES      10
#define MAX_BUFF        1024

#define INITIAL_TIME    5
#define INTERVAL        5

bool handleTcp(int, int*, struct epoll_event*);
bool handleUdp(int, int*, struct epoll_event*);
int handleTcpSyn(int, int*, struct epoll_event*);
bool handleTimer(int);
bool setTimer(int, int, int);

int main()
{
    struct epoll_event events[MAX_EVENTS], ev_tcp_listen, ev_tcp, ev_udp, ev_timer;
    int tcp_sock = -1, tcp_sock_listen = -1, udp_sock = -1, nfds = 0, epfd = -1, timer_fd = -1;
    struct sockaddr_in me;
    struct timespec current_time;
    struct itimerspec timeout_val;
    socklen_t sin_size = sizeof(struct sockaddr_in);

    //clear struct
    memset(events,          0x00, sizeof(struct epoll_event));
    memset(&ev_tcp,         0x00, sizeof(struct epoll_event));
    memset(&ev_tcp_listen,  0x00, sizeof(struct epoll_event));
    memset(&ev_udp,         0x00, sizeof(struct epoll_event));
    memset(&current_time,   0x00, sizeof(struct timespec));
    memset(&timeout_val,    0x00, sizeof(struct itimerspec));
    memset(&me,             0x00, sin_size);

    // set my address info
    me.sin_family = AF_INET;
    me.sin_port = htons(13400);
    me.sin_addr.s_addr = inet_addr(ME);

    //create time file discriptor
    if (0 > (timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)))
    {
        //if error occured
        fprintf(stderr, "Failed to create time file discriptor. errno=%d\n", errno);
        return NG;
    }

    if (false == setTimer(timer_fd, INTERVAL, 0))
    {
        //if error occured
        fprintf(stderr, "Failed to set timer. errno=%d\n", errno);
        return NG;
    }
    
    //create TCP socket
    if (0 > (tcp_sock_listen = socket(AF_INET, SOCK_STREAM, 0)))
    {
        //if error occured
        fprintf(stderr, "Failed to create tcp socket. errno=%d\n", errno);
        return NG;
    }
    else if (0 > bind(tcp_sock_listen, (struct sockaddr *)&me, sizeof(me)))
    {
        //if error occured
        fprintf(stderr, "Failed to bind tcp listen socket. errno=%d\n", errno);
        return NG;        
    }
    else
    {
        int yes = 1;
        setsockopt(tcp_sock_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
        setsockopt(tcp_sock_listen, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
        fprintf(stdout, "Successfully created tcp socket.\n");
    }

    //create UDP socket
    if (0 > (udp_sock = socket(AF_INET, SOCK_DGRAM, 0)))
    {
        //if error occured
        fprintf(stderr, "Failed to create udp socket. errno=%d\n", errno);
        return NG;
    }
    else if (0 > bind(udp_sock, (struct sockaddr *)&me, sizeof(me)))
    {
        //if error occured
        fprintf(stderr, "Failed to bind udp socket. errno=%d\n", errno);
        return NG;        
    }
    else
    {
        fprintf(stdout, "Successfully created udp socket.\n");
    }

    //listen
    if (0 > listen(tcp_sock_listen, MAX_QUEUES))
    {
        //if error occured
        fprintf(stderr, "Failed to listen to tcp socket. errno=%d\n", errno);
        return NG;
    }
    else
    {
        fprintf(stdout, "Successfully listened tcp socket.\n");
    }

    // create epoll instance
    if (0 > (epfd = epoll_create(MAX_EVENTS)))
    {
        fprintf(stderr, "Failed to create epoll instance. errno=%d", errno);
        return NG;
    }

    // set event
    //socket for listening to tcp
    ev_tcp_listen.events = EPOLLIN;    /* 入力待ち（読み込み待ち） */
    ev_tcp_listen.data.fd = tcp_sock_listen;
    if (0 > epoll_ctl(epfd, EPOLL_CTL_ADD, tcp_sock_listen, &ev_tcp_listen))
    {
        fprintf(stderr, "Failed to relate epoll instance with socket for listening to tcp. errno=%d\n", errno);
        return NG;
    }

    //socket for receiving udp
    ev_udp.events = EPOLLIN;    /* 入力待ち（読み込み待ち） */
    ev_udp.data.fd = udp_sock;
    if (0 > epoll_ctl(epfd, EPOLL_CTL_ADD, udp_sock, &ev_udp))
    {
        fprintf(stderr, "Failed to relate epoll instance with socket for receiving udp. errno=%d\n", errno);
        return NG;
    }

    //timer file descriptor
    ev_timer.events = EPOLLIN;    /* 入力待ち（読み込み待ち） */
    ev_timer.data.fd = timer_fd;
    if (0 > epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev_timer))
    {
        fprintf(stderr, "Failed to relate epoll instance with timer file descriptor. errno=%d\n", errno);
        return NG;
    }

    int timeout_cnt = 0;

    while (1)
    {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (0 > nfds)
        {
            //if error occured
            fprintf(stderr, "Failed to wait epoll. errno=%d\n", errno);
            return NG;
        }

        for (int i = 0 ; i < nfds ; i++)
        {
            if (tcp_sock == events[i].data.fd)
            {
                if (false == handleTcp(tcp_sock, &epfd, &ev_tcp)) return NG;
            }
            else if (udp_sock == events[i].data.fd)
            {
                if (false == handleUdp(udp_sock, &epfd, &ev_udp)) return NG;
            }
            else if (timer_fd == events[i].data.fd)
            {
                if (false == handleTimer(timer_fd)) return NG;
                timeout_cnt ++;

                if (0 == timeout_cnt % 2)
                {
                    if (false == setTimer(timer_fd, INTERVAL * timeout_cnt, 0))
                    {
                        //if error occured
                        fprintf(stderr, "Failed to set timer. errno=%d\n", errno);
                        return NG;
                    }
                }
            }
            else if (tcp_sock_listen == events[i].data.fd)
            {
                if (0 > (tcp_sock = handleTcpSyn(tcp_sock_listen, &epfd, &ev_tcp_listen))) return NG;
            }
            else
            {
                fprintf(stderr, "Something is wrong. nfds=%d, events[%d].data.fd=%d\n", nfds, i, events[i].data.fd);
                return NG;
            }
        }
    }


    return OK;
}


bool handleTcp(int socket, int* epfd, struct epoll_event* ev)
{
    int recv_size = 0;
    char buf[MAX_BUFF] = {0};
    
    fprintf(stdout, "Received tcp message\n");
    if (0 > (recv_size = recv(socket, buf, MAX_BUFF, 0)))
    {
        //if error occured
        fprintf(stderr, "Failed to recv tcp. errno=%d\n", errno);
        return false;
    }
    else if (0 == recv_size)
    {
        //if end-of-file
        fprintf(stderr, "End tcp socket.\n");
        
        if (0 > epoll_ctl(*epfd, EPOLL_CTL_DEL, socket, ev))
        {
            fprintf(stderr, "Failed to disconnect epoll instance with socket for receiving tcp. errno=%d\n", errno);
            return false;
        }
        else if (0 > shutdown(socket, SHUT_RDWR))
        {
            fprintf(stderr, "Failed to shutdown tcp socket. errno=%d\n", errno);
            return false;
        }
    }

    return true;
}
bool handleUdp(int socket, int* epfd, struct epoll_event* ev)
{
    int recv_size = 0;
    char buf[MAX_BUFF] = {0};
    
    fprintf(stdout, "Received udp message\n");
    if (0 > (recv_size = recvfrom(socket, buf, MAX_BUFF, 0, NULL, NULL)))
    {
        //if error occured
        fprintf(stderr, "Failed to recv udp. errno=%d\n", errno);
        return false;
    }
    else if (0 == recv_size)
    {
        //if end-of-file
        fprintf(stderr, "End udp socket.");
        
        if (0 > epoll_ctl(*epfd, EPOLL_CTL_DEL, socket, ev))
        {
            fprintf(stderr, "Failed to disconnect epoll instance with socket for receiving udp. errno=%d\n", errno);
            return false;
        }             
    }

    return true;
}

int handleTcpSyn(int socket, int* epfd, struct epoll_event* ev)
{
    int output_sock = -1;

    if (0 > (output_sock = accept(socket, NULL, NULL)))
    {
        //if error occured
        fprintf(stderr, "Failed to accept tcp socket. errno=%d\n", errno);
    }
    else
    {
        fprintf(stdout, "Successfully accepted tcp socket.\n");

        //socket for receiving tcp
        ev->events = EPOLLIN;    /* 入力待ち（読み込み待ち） */
        ev->data.fd = output_sock;
        if (0 > epoll_ctl(*epfd, EPOLL_CTL_ADD, output_sock, ev))
        {
            fprintf(stderr, "Failed to relate epoll instance with socket for receiving tcp. errno=%d\n", errno);
        }
    }

    return output_sock;
}

bool handleTimer(int fd)
{
    uint64_t exp;

    fprintf(stdout, "Timeout\n");

    if (0 > read(fd, &exp, sizeof(uint64_t)))
    {
        fprintf(stderr, "Failed to read timer file descriptor. errno=%d\n", errno);
        return false;
    }
    else
    {
        fprintf(stdout, "timerfd timeout.  exp=%lu\n", exp);
    }

    return true;
}

bool setTimer(int timer_fd, int second, int nanosecond)
{
    struct timespec current_time;
    struct itimerspec timer_val;

    //initialize
    memset(&current_time, 0x00, sizeof(struct timespec));
    memset(&timer_val, 0x00, sizeof(struct itimerspec));

    //get now
    if (0 > clock_gettime(CLOCK_MONOTONIC, &current_time))
    {
        //if error occured
        fprintf(stderr, "Failed to get current time. errno=%d\n", errno);
        return false;
    }

    //set timeout value
    timer_val.it_value.tv_sec     = current_time.tv_sec;
    timer_val.it_value.tv_nsec    = 0;
    timer_val.it_interval.tv_sec  = second;
    timer_val.it_interval.tv_nsec = nanosecond;

    //set timer
    if (0 > timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &timer_val, 0))
    {
        //if error occured
        fprintf(stderr, "Failed to set timer. errno=%d\n", errno);
        return false;
    }

    return true;
}