#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdarg.h>

int parse_options(int argc, char *argv[]);

char *bind_addr, *remote_host, *cmd_in, *cmd_out;
int remote_port;
bool foreground = FALSE;
bool use_syslog = FALSE;

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