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

#define BUF_SIZE 16384
#define LOG_CRIT 1

int parse_options(int argc, char *argv[]);
int create_connection();
void server_loop();
void handle_client(int client_sock, struct sockaddr_storage client_addr);
void forward_data(int source_sock, int dest_sock);
void forward_data_ext(int source_sock, int dest_sock, char *cmd);
void plog(int priority, const char *format, ...);
int create_socket(int port);

void sigchld_handler(int signal);
void sigterm_handler(int signal);

char *bind_addr, *remote_host, *cmd_in, *cmd_out;
int remote_port = 0, server_sock, client_sock, remote_sock;
int connections_processed = 0;
bool foreground = false;
bool use_syslog = false;

int main(int argc, char *argv[]){
    int local_port;
    pid_t pid;

    bind_addr = NULL;

    local_port = parse_options(argc, argv);

    if( local_port < 0 ){
        printf("error on local port %d", local_port);
        return local_port;
    }

    if( (server_sock = create_socket(local_port)) < 0){
        plog(1, "cannot run server: %d", server_sock);
        return server_sock;
    }

    signal(SIGCHLD, sigchld_handler);
    signal(SIGTERM, sigterm_handler);

    if( foreground ){
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

int parse_options(int argc, char *argv[]){
    int c, local_port = 0;

    while((c = getopt(argc, argv, "b:l:h:p:i:o:fs")) != -1 ){
        switch (c)
        {
        case 'l':
            local_port = atoi(optarg);
            break;
        
        case 'b':
            bind_addr = optarg;
            break;

        case 'h':
            remote_host = optarg;
            break;

        case 'p':
            remote_port = atoi(optarg);
            break;

        case 'i':
            cmd_in = optarg;
            break;

        case '0':
            cmd_out = optarg;
            break;

        case 'f':
            foreground = true;
            break;
        
        case 's':
            use_syslog = true;
            break;

        default:
            break;
        }
    }
}

void update_connection_count(){
    
}

void server_loop(){
    struct sockaddr_storage client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while(1){
        update_connection_count();
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);

        if( fork() == 0 ){
            close(server_sock);
            handle_client(client_sock, client_addr);
            exit(0);
        }else{
            connections_processed++;
        }
        close(client_sock);
    }
}

void handle_client(int client_sock, struct sockaddr_storage client_addr){
    
    if((remote_sock = create_connection()) < 0 ){
        plog(2, "Cannot connect to host %d", remote_sock);
        goto cleanup;
    }

    if( fork() == 0 ){
        if(cmd_out){
            forward_data_ext(client_sock, remote_sock, cmd_out);
        }else{
            forward_data(client_sock, remote_sock);
        }
        exit(0);
    }

    if( fork() == 0 ){
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

int create_connection(){
    struct addrinfo hints, *res=NULL;
    int sock;
    int validfamily = 0;
    char portstr[12];

    memset(&hints, 0x00, sizeof(hints));
    
    hints.ai_flags = AI_NUMBERICSERV;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    sprintf(poststr, "%d", remote_port);

    if(validfamily = check_ipversion(remote_host)){
        hints.ai_family = validfamily;
        hints.ai_flags |= AI_NUMERICHOST;
    }

    if( getaddrinfo(remote_host, portstr, &hints, &res) != 0 ){
        errno = EFAULT;
        return CLIENT_RESOLVE_ERROR;
    }

    if((sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0 ){
        return CLIENT_SOCKET_ERROR;
    }

    if( connect(sock, res->ai_addr, res->ai_addrlen) < 0 ){
        return CLIENT_CONNECT_ERROR;
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

    if( use_syslog){
        vsyslog(priority, format, ap);
    }else{
        vfprintf(stderr, format, ap);
        fprintf(stderr, "\n");
    }

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

int create_socket(int port){
    int server_sock, optval = 1;
    int validfamily = 0;
    struct addrinfo hints, *res = NULL;
    char portstr[12];

    memset(&hints, 0x00, sizeof(hints));
    server_sock = -1;

    hints.ai_flags = AI_NUMBERICSERV;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if( bind_addr != NULL) {
        if(validfamily = check_ipversion(bind_addr)){
            hints.ai_family = validfamily;
            hints.ai_flags |= AI_NUMERICHOST;
        }
    } else{
        hints.ai_family = AF_INET6;
        hints.ai_flags |= AI_PASSIVE;
    }

    sprintf(poststr, "%d", port);

    if( getaddrinfo(bind_addr, portstr, &hints, &res) != 0 ){
        return CLIENT_RESOLVE_ERROR;
    }

    if((server_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0){
        return SERVER_SOCKET_ERROR;
    }

    if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){
        return SERVER_SETSOCKETOPT_ERROR;
    }

    if( bind(server_sock, res->ai_addr, res->ai_addrlen) == -1){
        close(server_sock);
        return SERVER_BIND_ERROR;
    }

    if( listen(server_sock, BACKLOG) < 0){
        return SERVER_LISTEN_ERROR;
    }

    if( res!= NULL ){
        freeaddrinfo(res);
    }

    return server_sock;
}