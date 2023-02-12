#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdarg.h>

#define BUF_SIZE 16384

int parse_options(int argc, char *argv[]);
void server_loop();

char *bind_addr, *remote_host, *cmd_in, *cmd_out;
int remote_port = 0, server_sock, client_sock, remote_sock;
int connections_processed = 0;
bool foreground = FALSE;
bool use_syslog = FALSE;

int main(int argc, char *argv[]){
    int local_port;
    pid_t pid;

    bind_addr = NULL;

    local_port = parse_options(argc, argv);

    if( local_port < 0 ){
        printf("error on local port %d", local_port);
        return local_port;
    }

    if( use_syslog ){
        openlog("proxy", LOG_PID, LOG_DAEMON);
    }

    if( (server_sock = create_socket(local_port)) < 0){
        plog(LOG_CRIT, "cannot run server: %m");
        return server_sock;
    }

    signal(SIGCHLD, sigchld_handler);
    signal(SIGCHLD, sigchld_handler);

    if( foreground ){
        server_loop();
    } else {
        switch(pid = fork()){
            case 0:
                server_loop();
                break;
            case -1:
                plog(LOG_CRIT, "cannot daemon runing %m");
                return pid;
            default:
                close(server_sock);
                break;
        }
    }

    if( use_syslog ){
        closelog();
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
            foreground = TRUE;
            break;
        
        case 's':
            use_syslog = TRUE;
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
        plog(LOG_ERR, "Cannot connect to host %m");
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

