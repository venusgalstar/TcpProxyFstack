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
#include <sys/ioctl.h>

#include "ff_config.h"
#include "ff_api.h"
#include "http_parse.h"

#define MAX_EVENTS 512
#define RING_BUFFER_SIZE 16
#define MAX_PAYLOAD 4096

/* kevent set */
struct kevent kevSet;
/* events */
struct kevent events[MAX_EVENTS];
/* kq */
int kq;

/* remote and local configure*/
char local_port = 80;

/* socket for listenning from client and socket for remote access*/
int sockClient;
int sockRemote;
int nSockclient;
int newConnectionFlag = 0;

char bufferToRemote[RING_BUFFER_SIZE][MAX_PAYLOAD];
int bufferWritePoint = 0, bufferReadPoint = 0;

void dumpHex(char* s, int len )
{
    int i;

    for(i = 0; i < len; i++){
	printf("%c", s[i]);
	if(i%32 == 0)
	    printf("\n");
    }
}

int createserverSocket(char* remote_host, int remote_port){

    struct sockaddr_in remote_addr;
    int on = 1;
    int ret;
    int sock;
    
    bzero(&remote_addr, sizeof(remote_addr));

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    inet_pton(AF_INET, remote_host, &(remote_addr.sin_addr));

    printf("remote_host = %x, remote_port = %d\n", remote_addr.sin_addr.s_addr, remote_port);
//  remote_addr.sin_addr.s_addr = inet_addr(remote_host);

    sock = ff_socket(AF_INET, SOCK_STREAM, 0);

    if( sock < 0 ){
        printf("remote socket error%d %d %s\n", sock, errno, strerror(errno));
	    return -1;
    }

    ff_ioctl(sock, FIONBIO, &on);

    ret = ff_connect(sock, (struct linux_sockaddr*)&remote_addr, sizeof(remote_addr));

    if( ret < 0 && errno != EINPROGRESS){
        printf("remote socket result %d;%s\n", errno, strerror(errno));
	    return -1;
    }	    	
    else {
        printf("sucess %d %d %s\n", sock, errno, strerror(errno));
    }

    EV_SET(&kevSet, sock, EVFILT_WRITE, EV_ADD, 0, MAX_EVENTS, NULL);

    //assert((kq = ff_kqueue()) > 0);
    // Update kqueue 
    ff_kevent(kq, &kevSet, 1, NULL, 0, NULL);

    EV_SET(&kevSet, sock, EVFILT_READ, EV_ADD, 0, 0, NULL);

    //assert((kq = ff_kqueue()) > 0);
    // Update kqueue 
    ff_kevent(kq, &kevSet, 1, NULL, 0, NULL);
    return sock;
}

int loop(void *arg)
{
    /* Wait for events to happen */
    unsigned nevents = ff_kevent(kq, NULL, 0, events, MAX_EVENTS, NULL);
    unsigned i;
    int ret;
    char buf[4096];

    for (i = 0; i < nevents; ++i) {
        struct kevent event = events[i];
        int clientfd = (int)event.ident;
        /* Handle disconnect */
        if (event.flags & EV_EOF) {
            /* Simply close socket */
            ff_close(clientfd);
//	        printf("socket closed\n");
        } else if (clientfd == sockClient) {

            int available = (int)event.data;

            do {
                nSockclient = ff_accept(clientfd, NULL, NULL);
                newConnectionFlag = 1;

                printf("new connection was accepted! sock = %d\n", nSockclient);

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

        } else if(event.filter == EVFILT_WRITE ){

            if( bufferReadPoint != bufferWritePoint ){
                
                printf("sending data to remote = %d\n", bufferReadPoint);
                
                ssize_t writelen = ff_write(sockRemote, bufferToRemote[bufferReadPoint], strlen(bufferToRemote[bufferReadPoint]));

//                bufferReadPoint = (bufferReadPoint + 1) % RING_BUFFER_SIZE;

                if (writelen < 0){
                    printf("ff_write failed:%d, %s\n", errno,
                        strerror(errno));
                }else{
		    bufferReadPoint = (bufferReadPoint +1) % RING_BUFFER_SIZE;
		}
            }
            
        } else if (event.filter == EVFILT_READ) {

            ssize_t readlen = ff_read(clientfd, buf, sizeof(buf));

            if( clientfd == nSockclient ){

                buf[readlen] = '\0';
                
                if( newConnectionFlag == 1 ){
                    struct ParsedRequest *req;
                    req = ParsedRequest_create();
                    
                    if (ParsedRequest_parse(req, buf, strlen(buf)) < 0) {		
                        fprintf (stderr,"Error in request message..only http and get with headers are allowed ! \n");
                        exit(0);
                    }

                    if (req->port == NULL)  // if port is not mentioned in URL, we take default as 80 
                        req->port = (char *) "80";

                    printf("to %s:%s\n", req->host, req->port);

                    sockRemote = createserverSocket(req->host, atoi(req->port));

                    printf("new data was accepted! sock = %d, accepted = %d, remote = %d\n", clientfd, nSockclient,sockRemote);
                }

                newConnectionFlag = 0;

                memcpy(bufferToRemote[bufferWritePoint], buf, readlen);
                bufferWritePoint = (bufferWritePoint + 1) % RING_BUFFER_SIZE;

                printf("data to write = %d\n", bufferWritePoint);

            }else if( clientfd == sockRemote ){
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
