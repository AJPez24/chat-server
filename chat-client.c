/*
 * chat-client.c
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define BUF_SIZE 4096

void* child_func(void* data);

int main(int argc, char *argv[])
{
    char *dest_hostname, *dest_port;
    struct addrinfo hints, *res;
    int conn_fd;
    char buf[BUF_SIZE];
    int n;
    int rc;

    dest_hostname = argv[1];
    dest_port     = argv[2];

    //create a socket
    if ((conn_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1){
        perror("Couldn't create a socket");
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if((rc = getaddrinfo(dest_hostname, dest_port, &hints, &res)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(rc));
        exit(2);
    }

    //connect to the server
    if(connect(conn_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect");
        exit(3);
    }

    printf("Connected\n");

    pthread_t child_thread;

    if ((pthread_create(&child_thread, NULL, child_func, &conn_fd)) != 0){
        perror("Couldn't create thread");
        exit(4);
    }

    //infinite loop of sending the data
    while((n = read(0, buf, BUF_SIZE)) > 0) {
        buf[n -1] = '\0';
        if ((send(conn_fd, buf, n, 0)) == -1){
            perror("Couldn't send data");
            exit(5);
        }
    }

    if ((close(conn_fd)) != 0){
        perror("Couldn't close file descriptor");
        exit(6);
    }

    puts("Exiting.");

    return 0;
}

void* child_func(void* data){
    int my_conn_fd = *((int*) data);
    char buf[BUF_SIZE];
    int bytes_recieved;
    time_t mytime;
    char time_string[50];

    //infinite loop of recieving data from the connection
    while((bytes_recieved = recv(my_conn_fd, buf, BUF_SIZE, 0)) > 0){
        buf[bytes_recieved - 1] = '\0';
        if((mytime = time(NULL)) == (time_t) -1){
            perror("Couldn't get time");
            exit(7);
        }
        strftime(time_string, sizeof(time_string), "%H:%M:%S", localtime(&mytime));

        printf("%s: ", time_string);
        puts(buf);
    }

    if (bytes_recieved == 0){
        puts("Connection closed by remote host.");
    }
    else{
        perror("Lost connection from server");
    }

    exit(0);

    return NULL;
}
