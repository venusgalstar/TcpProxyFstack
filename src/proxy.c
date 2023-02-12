#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>

#define BUF_SIZE 16384
#define BACKLOG 20

int parse_options(int argc, char *argv[]);
int create_connection();
void server_loop();
void handle_client(int client_sock, struct sockaddr_storage client_addr);
void forward_data(int source_sock, int dest_sock);
void forward_data_ext(int source_sock, int dest_sock, char *cmd);
void plog(int priority, const char *format, ...);
int create_socket(int port);
int check_ipversion(char * addr);

void sigchld_handler(int signal);
void sigterm_handler(int signal);

char *bind_addr, *remote_host, *cmd_in, *cmd_out;
int remote_port = 0, server_sock, client_sock, remote_sock;
int connections_processed = 0;
bool foreground = false;

// main function of proxy
// receive parameters from running command for remote host and port, bind addr and port
// client --- local --- remote

int main(int argc, char *argv[]){

    // local port for binding
    int local_port;

    // process id of proxy
    pid_t pid;

    // address for binding
    bind_addr = NULL;

    // get local port from running command
    local_port = parse_options(argc, argv);

    // if command doesn't include local port proxy will be terminated
    if( local_port < 0 ){
        printf("error on local port %d \n", local_port);
        return local_port;
    }

    printf("succeed in parsing parameters from command\n");

    // create socket of binding
    if( (server_sock = create_socket(local_port)) < 0){
        plog(1, "cannot run server: %d", server_sock);
        return server_sock;
    }

    printf("succeed in creating proxy socket\n");

    // return 0;

    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);

    if( foreground ){
        
        printf("proxy server run as foreground\n");

        server_loop();
    } else {
        switch(pid = fork()){
            case 0:
                server_loop();
                break;
            case -1:
                plog(1, "cannot daemon runing");
                return pid;
            default:
                close(server_sock);
                break;
        }
    }

    return EXIT_SUCCESS;
}

// parse parameters from running command, specified by option

int parse_options(int argc, char *argv[]){
    int c = 0, local_port = 0;
    // c = getopt(argc, argv, "b:l:h:p:i:o:f");
    printf("%d\n", argc);

    while((c = getopt(argc, argv, "b:l:h:p:i:o:f")) != -1 ){
        switch (c)
        {
        case 'l': // optarg will be local port, string to integer
            printf(" bind port = %s\n", optarg);
            local_port = atoi(optarg);
            break;
        
        case 'b': // optarg will be bind addr for local
            printf(" bind addr = %s\n", optarg);
            bind_addr = optarg;
            break;

        case 'h': // optarg will be remote addr
            printf(" remote addr = %s\n", optarg);
            remote_host = optarg;
            break;

        case 'p': // optarg will be remote port, string to integer
            printf(" remote port = %s\n", optarg);
            remote_port = atoi(optarg);
            break;

        case 'i':
            cmd_in = optarg;
            break;

        case 'o':
            cmd_out = optarg;
            break;

        case 'f': // proxy will be running as foreground
            printf(" proxy run as foreground\n");
            foreground = true;
            break;

        default:
            break;
        }
    }

    return local_port;
}

void update_connection_count(){
    
}

// proxy loop, accepting connection from client and forward to remote
void server_loop(){
    struct sockaddr_storage client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while(1){
        update_connection_count();

        printf("listenning connection from client\n");
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);

        printf("accepted connection from client\n");

        if( fork() == 0 ){

            printf("succed in fork\n");
            close(server_sock);
            handle_client(client_sock, client_addr);
            exit(0);
        }else{

            printf("succed in background\n");
            connections_processed++;
        }
        close(client_sock);
    }
}

// data switching function
void handle_client(int client_sock, struct sockaddr_storage client_addr){
    
    // creating a socket to remote
    if((remote_sock = create_connection()) < 0 ){
        plog(2, "Cannot connect to host %d", remote_sock);
        goto cleanup;
    }

    if( fork() == 0 ){
        // proxy run as foreground and send data from client to remote
        if(cmd_out){
            forward_data_ext(client_sock, remote_sock, cmd_out);
        }else{
            forward_data(client_sock, remote_sock);
        }
        exit(0);
    }

    if( fork() == 0 ){
        // proxy run as foreground and send data from remote to client
        if(cmd_in){
            forward_data_ext(remote_sock, client_sock, cmd_in);
        } else{
            forward_data(remote_sock, client_sock);
        }
        exit(0);
    }

cleanup:
    close(remote_sock);
    close(client_sock);
}

void forward_data(int source_sock, int dest_sock){

    // initializing buffer to recv data from apart, buf is 16KB limited.
    ssize_t n;
    char buffer[BUF_SIZE];

    while((n = recv(source_sock, buffer, BUF_SIZE, 0)) > 0){
        send(dest_sock, buffer, n, 0);
    }

    shutdown(dest_sock, SHUT_RDWR);
    close(dest_sock);

    shutdown(source_sock, SHUT_RDWR);
    close(source_sock);
}

void forward_data_ext(int source_sock, int dest_sock, char *cmd){
    char buffer[BUF_SIZE];
    int n;
}

// create a socket to remote
int create_connection(){
    struct addrinfo hints, *res=NULL;
    int sock;
    int validfamily = 0;
    char portstr[12];

    // initializing memory
    memset(&hints, 0x00, sizeof(hints));
    
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // initializing port from running command
    sprintf(portstr, "%d", remote_port);

    // check version of remote addr
    if(validfamily = check_ipversion(remote_host)){
        hints.ai_family = validfamily;
        hints.ai_flags |= AI_NUMERICHOST;
    }

    if( getaddrinfo(remote_host, portstr, &hints, &res) != 0 ){
        errno = EFAULT;
        return 1;
    }

    // create a socket to remote addr/port
    if((sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0 ){
        return 2;
    }

    // trying to connect
    if( connect(sock, res->ai_addr, res->ai_addrlen) < 0 ){
        return 3;
    }

    if( res != NULL ){
        freeaddrinfo(res);
    }

    return sock;
}

int check_ipversion(char * addr){
    struct in6_addr bindaddr;

    if(inet_pton(AF_INET, addr, &bindaddr) == 1){
        return AF_INET;
    }else{
        if(inet_pton(AF_INET6, addr, &bind_addr) == 1){
            return AF_INET6;
        }
    }
    return 0;
}

void plog(int priority, const char *format, ...){
    va_list ap;
    va_start(ap, format);

    vfprintf(stderr, format, ap);
    fprintf(stderr, "\n");
    
    va_end(ap);
}

void sigchld_handler(int signal){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void sigterm_handler(int signal){
    close(client_sock);
    close(server_sock);
    exit(0);
}

// it will create proxy socket from client
int create_socket(int port){

    // server_sock fd
    int server_sock, optval = 1;
    int validfamily = 0;
    struct addrinfo hints, *res = NULL;
    char portstr[12];

    // initializing memory of addrinfo
    memset(&hints, 0x00, sizeof(hints));
    server_sock = -1;

    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // if there is no bind addr from command, it will be ipv6
    if( bind_addr != NULL) {
        if(validfamily = check_ipversion(bind_addr)){
            hints.ai_family = validfamily;
            hints.ai_flags |= AI_NUMERICHOST;
        }
    } else{
        hints.ai_family = AF_INET6;
        hints.ai_flags |= AI_PASSIVE;
    }

    // init portstr as port
    sprintf(portstr, "%d", port);

    // check addr info for proxy socket
    if( getaddrinfo(bind_addr, portstr, &hints, &res) != 0 ){
        return 1;
    }

    // create a proxy socket from addr
    if((server_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0){
        return 2;
    }

    // initialize socket 
    if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){
        return 3;
    }

    // bind socket as addr
    if( bind(server_sock, res->ai_addr, res->ai_addrlen) == -1){
        close(server_sock);
        return 4;
    }

    // listen connection from client
    if( listen(server_sock, BACKLOG) < 0){
        return 5;
    }

    if( res!= NULL ){
        freeaddrinfo(res);
    }

    return server_sock;
}