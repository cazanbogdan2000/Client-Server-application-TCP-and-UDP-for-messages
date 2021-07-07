#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <bits/stdc++.h>
#include "helpers.h"
#include <netinet/tcp.h>

using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_address server_port\n", file);
	exit(0);
}

// functie care returneaza -1 in caz de eroare, 0 sau 1 care reprezinta sf pentru
// subscribe, si un numar mai mare decat 0 daca este vorba de unsubscribe
// se va returna prin efect lateral, in new_topic, numele topicului citit de la
// tastatura
int get_topic (char* new_topic, char* buffer, bool action_type) {
    // cazul subscribe, luam topicul si SF
    if (action_type == false) {
        int ok = 0;
        int start = 10, stop;
        for (int i = start; i < BUFLEN; i++) {
            if (buffer[i] == ' ' && ok == 0) {
                start = i;
                continue;
            }
            else if (buffer[i] != ' ' && (ok == 0 || ok == 1)) {
                ok = 1;
            }
            else if (buffer[i] == ' ' && ok == 1) {
                stop = i;
                memcpy(new_topic, buffer + start, stop - start);
                new_topic[stop - start] = '\0';
                ok = 2;
            }
            else if (buffer[i] != ' ' && ok == 2) {
                return buffer[i] == '0' ? 0 : 1;
            }
        }
    }
    // cazul unsubscribe, asemanator cu primul caz, diferente minuscule;
    // luam topicul doar de data asta
    else {
        int ok = 0;
        int start = 12, stop;
        for (int i = start; i < BUFLEN; i++) {
            if (buffer[i] == ' ' && ok == 0) {
                start = i;
                continue;
            }
            else if ((buffer[i] == '\n' || buffer[i] == ' ') && ok == 1) {
                stop = i;
                memcpy(new_topic, buffer + start, stop - start);
                new_topic[stop - start] = '\0';
                return 2;
            }
            else if (buffer[i] != ' ') {
                ok = 1;
            }
            
        }
    }
    return -1;
}

// functie care initializeaza datele de baza pentru un client, cum ar fi
// conexiunea la server
void init(int sockfd, int argc, char* argv[], int ret, char* ID) {
    struct sockaddr_in serv_addr;
    if (argc < 4) {
        usage(argv[0]);
    }
    // TCP NO DELAY -- dezactivarea algoritmului lui Nagle
    int flag_delay = 1;
    int tcp_no_delay =
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag_delay, sizeof(int));
    
    serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");
    memcpy(ID, argv[1], strlen(argv[1]));
    // trimitem o cerere de conexiune catre server
    ret = connect(sockfd, (struct sockaddr*) &serv_addr,
        (socklen_t)sizeof(serv_addr)); 
    DIE(ret < 0, "connect tcp");
    // dupa acceptare, vom trimite ID-ul clientului
    send(sockfd, argv[1], strlen(argv[1]), 0);
}

// functie care primeste un mesaj de la server, care a dat mai deprtare mesajul
// primit de la clientul udp, si afiseaza outputul dorit
void message_from_server(struct msg2subscr received_msg) {
    // mesajul are tipul 0, adica este INT
    if (received_msg.type == 0) {
        // daca sign == 0, atunci avem semnul plus pentru numarul trimis
        if (received_msg.sign == 0) {
            printf("%s:%d - %s - INT - %d\n", inet_ntoa(received_msg.addr), 
                htons(received_msg.port), received_msg.topic, received_msg.int_value);
        }
        // sign == 1, semnul este -
        else {
            printf("%s:%d - %s - INT - -%d\n", inet_ntoa(received_msg.addr),
                htons(received_msg.port), received_msg.topic, received_msg.int_value);
        }
    }
    // mesajul are tipul 1, adica este SHORT_REAL
    else if (received_msg.type == 1) {
        printf("%s:%d - %s - SHORT_REAL - %.2f\n", inet_ntoa(received_msg.addr),
            htons(received_msg.port), received_msg.topic,
            (float)received_msg.short_real_value / 100);
    }
     // mesajul are tipul 2, adica este FLOAT
    else if (received_msg.type == 2) {
        if (received_msg.sign == 0) {
            // daca sign == 0, atunci avem semnul plus pentru numarul trimis
            printf("%s:%d - %s - FLOAT - %f\n", inet_ntoa(received_msg.addr),
                htons(received_msg.port), received_msg.topic,
                (float)received_msg.exponent / pow(10, received_msg.mantisa));
        }
        // sign == 1, semnul este -
        else {
            printf("%s:%d - %s - FLOAT - -%f\n", inet_ntoa(received_msg.addr),
                htons(received_msg.port), received_msg.topic,
                (float)received_msg.exponent / pow(10, received_msg.mantisa));
        }   
    }
    // mesajul are tipul 3, adica avem un string
    else {
        printf("%s:%d - %s - STRING - %s\n", inet_ntoa(received_msg.addr),
            htons(received_msg.port), received_msg.topic, received_msg.string_value);
    }
}

// functie care, atunci cand subscriber-ul primeste de la tastatura un mesaj cu
// continutul "subscribe", face prelucrarile necesare
void subscribe_to_topic(struct msg2srv to_send, int ret, int sockfd) {
    memcpy(to_send.action, "subscribe", 10);
    memset(to_send.topic, 0, strlen(to_send.topic));
    ret = get_topic(to_send.topic, buffer, false);
    DIE(ret < 0, "subscribe");
    to_send.SF = ret;
    printf("Subscribed to topic.\n");
    // trimitem catre server un mesaj de de abonare de la un anumit topic
    send(sockfd, &to_send, sizeof(struct msg2srv), 0);
}

// functie care, atunci cand subscriber-ul primeste de la tastatura un mesaj cu
// continutul "unsubscribe", face prelucrarile necesare
void un_subscribe_to_topic(struct msg2srv to_send, int ret, int sockfd) {
    memcpy(to_send.action, "unsubscribe", 12);
    memset(to_send.topic, 0, strlen(to_send.topic));
    ret = get_topic(to_send.topic, buffer, true);
    DIE(ret < 0, "unsubscribe");
    printf("Unsubscribed to topic.\n");
    // trimitem catre server un mesaj de de dezabonare de la un anumit topic
    send(sockfd, &to_send, sizeof(struct msg2srv), 0);
}

int main (int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    // sockfd -- file descriptor pentru socketul initial
    // newsockfd -- file descriptor pentru noul socket obtinut
    //              in urma acceptarii clientului TCP
    // portno -- port number, se foloseste in partea de initializare
    int sockfd, newsockfd, portno;
    int n, ret, fdmax;
    struct msg2subscr received_msg; // mesaj primit de la server
    struct msg2srv to_send; // mesaj ce trebuie trimis catre server
    char ID[11]; // ID-ul ce trebuie trimis catre server
    // in read_fds se tin socketii deschisi (in cazul subscriberilor, unul in
    // parte) plus socketul pentru stdin + un set auxiliar
    fd_set read_fds, tmp_fds;
    // get socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket tcp subscriber");
    fdmax = sockfd;

    // initializarea subscriberului, care trimite si id-ul catre serverul
    init(sockfd, argc, argv, ret, ID);

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    FD_SET(sockfd, &read_fds);

    while (1) {
        tmp_fds = read_fds;
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select tcp");
        // se primeste un mesaj pe socket, de la server
        if(FD_ISSET(sockfd, &tmp_fds)) {
            memset(&received_msg, 0, sizeof(received_msg));
            int receive = recv(sockfd, &received_msg, sizeof(received_msg), 0);
            DIE(receive < 0, "Not received from server");
            // cazul in care se primeste un mesaj de tip exit, atunci clientul
            // se va inchide, odata cu serverul
            if(strncmp(received_msg.command, "exit", 4) == 0) {
                close(sockfd);
                exit(0);
            }
            // daca continutul mesajului este de tip duplicate, atunci clientul
            // cu ID-ul respectiv este deja activ pe server; prin urmare, acest
            // client in care ne aflam va fi inchis
            else if (strncmp(received_msg.command, "duplicate", 9) == 0) {
                close(sockfd);
                exit(0);
            }
            // se primeste un mesaj de la udp, prin intermediul brokerului
            else {
                message_from_server(received_msg);
            }
        }
        // se citeste o anumita comanda de la tastatura
        else if (FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            memset(buffer, 0, BUFLEN);
            if(!fgets(buffer, BUFLEN - 1, stdin)){
                DIE(false, "read error");
            }
			
            if (strncmp(buffer, "exit", 4) == 0) {
				break;
			}
            // clientul se aboneaza la un topic
            else if (strncmp(buffer, "subscribe", 9) == 0) {
                subscribe_to_topic(to_send, ret, sockfd);
            }
            // clientul se dezaboneaza de la un topic
            else if (strncmp(buffer, "unsubscribe", 11) == 0) {
                un_subscribe_to_topic(to_send, ret, sockfd);
            }
        }
    }
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);
    close(sockfd);
    return 0;
}
