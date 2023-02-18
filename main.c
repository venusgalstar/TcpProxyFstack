#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>

#include "ff_config.h"
#include "ff_api.h"

#define MAX_EVENTS 512

/* kevent set */
struct kevent kevSet;
/* events */
struct kevent events[MAX_EVENTS];
/* kq */
int kq;

/* remote and local configure*/
char remote_host[20] = "95.217.33.149";
char remote_port = 80;
char local_port = 80;

/* socket for listenning from client and socket for remote access*/
int sockClient;
int sockRemote;

int loop(void *arg)
{
    /* Wait for events to happen */
    unsigned nevents = ff_kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
    unsigned i;
    int nSockclient;
    char portstr[20];

    sprintf(portstr, "%d", remote_port);

    for (i = 0; i < nevents; ++i) {
        struct kevent event = events[i];
        int clientfd = (int)event.ident;

        int ret;
        struct sockaddr_in remote_addr;

        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(remote_port);
        remote_addr.sin_addr.s_addr = inet_addr(remote_host);

        /* Handle disconnect */
        if (event.flags & EV_EOF) {
            /* Simply close socket */
            ff_close(sockClient);
            ff_close(sockRemote);

        } else if (clientfd == sockClient) {

            int available = (int)event.data;

            do {
                nSockclient = ff_accept(clientfd, NULL, NULL);

                printf("new connection was accepted!\n");

                if (nSockclient < 0) {
                    printf("ff_accept failed:%d, %s\n", errno,
                        strerror(errno));
                    break;
                }

                /* Add to event list */
                EV_SET(&kevSet, nSockclient, EVFILT_READ, EV_ADD, 0, 0, NULL);

                if(ff_kevent(kq, &kevSet, 1, NULL, 0, NULL) < 0) {
                    printf("ff_kevent error:%d, %s\n", errno,
                        strerror(errno));
                    return -1;
                }

                available--;
            } while (available);

            if (sockRemote < 0) {
                printf("ff_socket failed, sockRemote:%d, errno:%d, %s\n", sockRemote, errno, strerror(errno));
                exit(1);
            }

            ret = ff_connect(sockRemote, (struct linux_sockaddr*)&remote_addr, sizeof(remote_addr));

            EV_SET(&kevSet, sockRemote, EVFILT_READ, EV_ADD, 0, MAX_EVENTS, NULL);
            /* Update kqueue */
            ff_kevent(kq, &kevSet, 1, NULL, 0, NULL);

        } else if (event.filter == EVFILT_READ) {
            char buf[16384];

            if( clientfd == nSockclient ){
                ssize_t readlen = ff_read(clientfd, buf, sizeof(buf));
                ssize_t writelen = ff_write(sockRemote, buf, readlen);
                if (writelen < 0){
                    printf("ff_write failed:%d, %s\n", errno,
                        strerror(errno));
                    ff_close(clientfd);
                }
            }else if( clientfd == sockRemote ){
                ssize_t readlen = ff_read(clientfd, buf, sizeof(buf));
                ssize_t writelen = ff_write(nSockclient, buf, readlen);
                if (writelen < 0){
                    printf("ff_write failed:%d, %s\n", errno,
                        strerror(errno));
                    ff_close(clientfd);
                }
            }
        } else {
            printf("unknown event: %8.8X\n", event.flags);
        }
    }
}

int main(int argc, char * argv[])
{
    ff_init(argc, argv);

    assert((kq = ff_kqueue()) > 0);

    sockClient = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (sockClient < 0) {
        printf("ff_socket failed, sockClient:%d, errno:%d, %s\n", sockClient, errno, strerror(errno));
        exit(1);
    }

    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(80);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int ret = ff_bind(sockClient, (struct linux_sockaddr *)&my_addr, sizeof(my_addr));
    if (ret < 0) {
        printf("ff_bind failed, sockClient:%d, errno:%d, %s\n", sockClient, errno, strerror(errno));
        exit(1);
    }

     ret = ff_listen(sockClient, MAX_EVENTS);
    if (ret < 0) {
        printf("ff_listen failed, sockClient:%d, errno:%d, %s\n", sockClient, errno, strerror(errno));
        exit(1);
    }

    EV_SET(&kevSet, sockClient, EVFILT_READ, EV_ADD, 0, MAX_EVENTS, NULL);
    /* Update kqueue */
    ff_kevent(kq, &kevSet, 1, NULL, 0, NULL);

    ff_run(loop, NULL);
    return 0;
}
