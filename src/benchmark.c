#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define MAX_SIZE 1
struct arg_struct{
    int req;
    int rec;
    int time;
};

void *connection_handler(void *args);

int main(int argc, char **argv)
{
    int socket_desc , new_socket , c , *new_sock, i;

    char *char_clients = argv[1];
    int clients = atoi(char_clients);
    char *char_time = argv[2];
    int time = atoi(char_time);

    struct arg_struct args;
    args.req = 0;
    args.rec = 0;
    args.time = time;

    pthread_t sniffer_thread;
    for (i = 1; i <= clients; i++) {
        if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*)&args) < 0)
        {
            perror("could not create thread");
            return 1;
        }
    }

    sleep(time);

    printf("Benchmarking: localhost 9090");
    printf("\n");
    printf(
        "%d clients, running 1 bytes, %d sec.",
        clients, time 
    );
    printf("\n");
    printf(
        "Speed: %ld request/sec, %ld response/sec",
        args.req / time,
        args.rec / time
    );
    printf("\n"),
    printf("Requests: %ld", args.req);
    printf("\n");
    printf("Responses: %ld", args.rec);
    printf("\n");
    pthread_exit(NULL);
    return 0;
}

void *connection_handler(void *arguments)
{
    int sock_desc;
    struct sockaddr_in serv_addr;
    struct arg_struct *args = (struct arg_struct *)arguments;

    if((sock_desc = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        printf("Failed creating socket\n");

    bzero((char *) &serv_addr, sizeof (serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(9090);

    if (connect(sock_desc, (struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0) {
        printf("Failed to connect to server\n");
    }

    time_t endwait;
    time_t start = time(NULL);
    time_t seconds = args->time; // end loop after this time has elapsed
    endwait = start + seconds;
    while(start < endwait)
    {
        char sbuff[1] = "1";
        char rbuff[1];
        send(sock_desc,sbuff, 1,0);
        args->req += 1;
        if(recv(sock_desc,rbuff,MAX_SIZE,0)==0)
          printf("Error");
        else
          fputs(rbuff,stdout);
          args->rec += 1;


        bzero(rbuff,MAX_SIZE);
        //sleep(1);
        start = time(NULL);
    }
    close(sock_desc);
    return 0;
}