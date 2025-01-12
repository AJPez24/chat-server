/*
 * chat-server.c
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BACKLOG 10
#define BUF_SIZE 4096
#define NAME_SIZE 32

struct thread_data{
    int my_conn_fd;
    char* my_remote_ip;
    uint16_t my_remote_port;

    int bytes_received;
    char buf[BUF_SIZE];
    char clientname[NAME_SIZE];
    char oldname[NAME_SIZE];
    int message_size;
};

struct client{
    int fd;
    struct client* next_client;
};

void* child_func(void* data);
void send_message(char* message, int size);
void free_all_clients();

pthread_mutex_t mutex;

int listen_fd;

struct client* head;

int main(int argc, char *argv[])
{
    int conn_fd;
    char *listen_port;
    struct sockaddr_in remote_sa;
    socklen_t addrlen;
    struct addrinfo hints, *res;
    int rc;
    char* remote_ip;
    uint16_t remote_port;

    listen_port = argv[1];

    if((head = malloc(sizeof(struct client))) == NULL){
        perror("Couldn't allocate space for clients");
        exit(1);
    }
    
    head->fd = -1;
    head->next_client = NULL;

    // create a socket
    if ((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1){
        perror("Couldn't create a socket");
        exit(2);
    }

    // bind it to a port
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if((rc = getaddrinfo(NULL, listen_port, &hints, &res)) != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(rc));
        exit(3);
    }

    if ((bind(listen_fd, res->ai_addr, res->ai_addrlen)) == -1){
        perror("Couldn't bind socket to a port");
        exit(4);
    }

    // start listening
    if((listen(listen_fd, BACKLOG)) == -1){
        perror("Couldn't listen to socket");
        exit(5);
    }

    // infinite loop of accepting new connections and handling them 
    while(1) {
        // accept a new connection (will block until one appears)
        addrlen = sizeof(remote_sa);

        if ((conn_fd = accept(listen_fd, (struct sockaddr *) &remote_sa, &addrlen)) == -1){
            perror("Couldn't accept new connection");
            exit(6);
        }
        
        pthread_mutex_lock(&mutex);
        //If it's the first client
        if (head->fd == -1){
            head->fd = conn_fd;
            head->next_client = NULL;
        }

        else{
            struct client* current_client = head;
            while (current_client->next_client != NULL){
                current_client = current_client->next_client;
            }

            struct client* new_client;
            if((new_client = malloc(sizeof(struct client))) == NULL){
                perror("Couldn't allocate space for client");
                exit(7);
            }
            new_client->fd = conn_fd;
            new_client->next_client = NULL;
            current_client->next_client = new_client;
        }
        pthread_mutex_unlock(&mutex);

        // announce our communication partner
        remote_ip = inet_ntoa(remote_sa.sin_addr);
        remote_port = ntohs(remote_sa.sin_port);
        printf("new connection from %s:%d\n", remote_ip, remote_port);

        struct thread_data mydata;
        mydata.my_conn_fd = conn_fd;
        mydata.my_remote_ip = remote_ip;
        mydata.my_remote_port = remote_port;

        pthread_t child_thread;
        if ((pthread_create(&child_thread, NULL, child_func, &mydata)) != 0){
            perror("Couldn't create thread");
            exit(8);
        }
    }

    free_all_clients(head);
}

void* child_func(void* data){
    struct thread_data* my_data;
    if((my_data = malloc(sizeof(struct thread_data))) == NULL){
        perror("Couldn't allocate space for client data");
        exit(9);
    }

    *my_data = *((struct thread_data*) data);
    strcpy(my_data->clientname, "unknown");

    char* message;

    // receive and send according data until the other end closes the connection
    while((my_data->bytes_received = recv(my_data->my_conn_fd, my_data->buf, BUF_SIZE, 0)) > 0) {
        my_data->buf[my_data->bytes_received - 1] = '\0';

        //Check to see if the bytes have the /nick command
        //If they do, then print a server-side message, and don't send to the client
        if ((strncmp(my_data->buf, "/nick ", 6)) == 0){
            memcpy(my_data->oldname, my_data->clientname, NAME_SIZE);
            memcpy(my_data->clientname, my_data->buf + 6, my_data->bytes_received - 6);
            printf("User %s (%s:%d) is now known as %s.\n", my_data->oldname, my_data->my_remote_ip, my_data->my_remote_port, my_data->clientname);
            my_data->message_size = (NAME_SIZE * 2) + sizeof(my_data->my_remote_ip) + sizeof(my_data->my_remote_port) + 35;

            if((message = malloc(my_data->message_size)) == NULL){
                perror("Couldn't allocate space for the message");
                exit(10);
            }
            
            snprintf(message, my_data->message_size,"User %s (%s:%d) is now known as %s.", my_data->oldname, my_data->my_remote_ip, my_data->my_remote_port, my_data->clientname);

            if ((fflush(stdout)) == EOF){
                perror("Couldn't flush standard out");
                exit(11);
            }

            send_message(message, my_data->message_size);

            free(message);
        }

        //If they don't have the /nick command, then send the bytes to the client
        else{
            my_data->message_size = (NAME_SIZE + my_data->bytes_received + 2); //2 for the ':' and ' ' characters

            if((message = malloc(my_data->message_size)) == NULL){
                perror("Couldn't allocate space for the message");
                exit(12);
            }

            snprintf(message, my_data->message_size, "%s: %s", my_data->clientname, my_data->buf);

            if ((fflush(stdout)) == EOF){
                perror("Couldn't flush standard out");
                exit(13);
            }

            send_message(message, my_data->message_size);

            free(message);
        }
    }

    pthread_mutex_lock(&mutex);
    if (head->fd == my_data->my_conn_fd){
        //If the last client disconnects, need to keep the head pointer
        if (head->next_client == NULL){
            head->fd = -1;
        }

        else{
            struct client* old_head = head;
            head = head->next_client;
            free(old_head);
        }
    }

    else{
        struct client* current_client = head;
        while (current_client->next_client != NULL){
            if (current_client->next_client->fd == my_data->my_conn_fd){
                struct client* client_to_remove = current_client->next_client;
                current_client->next_client = client_to_remove->next_client;
                free(client_to_remove);
                break;
            }
            current_client = current_client->next_client;
        }
    }

    pthread_mutex_unlock(&mutex);

    my_data->message_size = (NAME_SIZE + sizeof(my_data->my_remote_ip) + sizeof(my_data->my_remote_port) + 30);

    if((message = malloc(my_data->message_size)) == NULL){
        perror("Couldn't allocate space for the message");
        exit(14);
    }

    snprintf(message, my_data->message_size, "User %s (%s:%d) has disconnected.", my_data->clientname, my_data->my_remote_ip, my_data->my_remote_port);

    send_message(message, my_data->message_size);

    free(message);

    printf("Lost connection from %s\n", my_data->clientname);

    close(my_data->my_conn_fd);
    free(my_data);

    return NULL;
}

//Sends the message 'text' to all of the connected users 
void send_message(char* text, int size){
    pthread_mutex_lock(&mutex);

    struct client* current_client = head;

    while (current_client != NULL){

        if (current_client->fd != -1){
            if ((send(current_client->fd, text, size, 0)) == -1){
                perror("Couldn't send message to client");
            }
        }
        current_client = current_client->next_client;
    }
    pthread_mutex_unlock(&mutex);
}

// Frees all allocations in the linked list starting at 'head'
void free_all_clients(){
    pthread_mutex_lock(&mutex);

    struct client* current_client = head;
    while (current_client->next_client != NULL){
        struct client* client_to_remove = current_client;
        current_client = current_client->next_client;
        free(client_to_remove);
    }
    free(current_client);

    pthread_mutex_unlock(&mutex);
}
