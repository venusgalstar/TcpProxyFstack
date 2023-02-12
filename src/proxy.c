#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>


int parse_options(int argc, char *argv[]);

char *bind_addr, *remote_host, *cmd_in, *cmd_out;

int main(int argc, char *argv[]){
    int local_port;
    pid_t pid;

    bind_addr = NULL;

    local_port = parse_options(argc, argv);
}

int parse_options(int argc, char *argv[]){
    int c, local_port = 0;

    while((c = getopt(argc, argv, "b:l:h:p:i:o:fs")) != -1 ){
        switch (c)
        {
        case 'L':
            local_port = atoi(optarg);
            break;
        
        default:
            break;
        }
    }
}