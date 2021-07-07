#include <stdio.h>
#include <iostream>
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

int i, j, k, t; // iteratori pe care ii vom folosi la greu

struct Server {
    vector<struct Client*> clients;
    // sockfd_tcp -- file descriptor pentru socketul initial al unui client tcp
    // sockfd_udp -- file descriptor pentru socketul initial al unui client udp
    // newsockfd -- file descriptor pentru noul socket obtinut
    //              in urma acceptarii clientului TCP
    // portno -- port number, se foloseste in partea de initializare
    int sockfd_tcp, sockfd_udp, newsockfd, portno;
    // tcp_addr -- adresa socketului destinat clientului tcp
    // udp_addr -- adresa socketului destinat clientului udp
    struct sockaddr_in tcp_addr, udp_addr;
    int n, connection_ret; bool ok;// rezultate de control intoarse de anumite functii
    // clilen -- dimensiunea adresei socketului destinat clientului
    socklen_t clilen;

    // read_fds -- set de file descriptori obtinuti in urma lui select
    // tmp_fds -- joaca rol de set auxiliar, pentru a pastra mereu read_fds intact
    // fdmax -- retine practic ultimul socket introdus in read_fds, care, in general, e si cel mai mare
    fd_set read_fds;
    fd_set tmp_fds;
    int fdmax; // file descriptorul maxim

    // vector in care se retin mesajele cu SF = 1 ce trebuie trimise clientilor
    // care au fost deconectati in timpul respectiv
    vector<struct msg2subscr *> sf_queue;
    // vector de topice cu sf = 0
    vector<struct Subscription_no_SF*> no_sf_topics;
    // vector de topice cu sf = 1
    vector<struct Subscription_SF*> sf_topics;
}server;

struct Client {
    int client_sockfd; // file descriptor pentru socketul clientului
    char ID[11]; // un string ce reprezinta ID-ul unic al clientului
}client;

struct Subscription_no_SF {
    char topic[TOPIC_LEN];
    vector<string> subscripted_clients; // retinem doar id-ul abonatului
}subscription_no_sf;

struct Subscription_SF {
    char topic[TOPIC_LEN];
    vector<string> subscripted_clients; // retinem doar id-ul abonatului
} subscription_sf;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

// functie care initializeaza serverul nostru
void init(struct Server* server, int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    server->ok = 0;
    // numar gresit de parametri din linia de comanda
    if (argc < 2) {
		usage(argv[0]);
	}

    // Facem seturile sa fie initial 0 (goale), pentru a nu aparea probleme ciudate
    FD_ZERO(&(server->read_fds));
    FD_ZERO(&(server->read_fds));

    // Deschidem socketul pentru client tcp
    server->sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    DIE(server->sockfd_tcp < 0, "Socket tcp err");
    // TCP NO DELAY
    int flag_delay = 1;
    int tcp_no_delay =
        setsockopt(server->sockfd_tcp, IPPROTO_TCP, TCP_NODELAY, &flag_delay, sizeof(int));

    // Deschidem socketul pentru client udp
    server->sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(server->sockfd_udp < 0, "Socket udp err");

    // Portul primit din linia de comanda
    server->portno = atoi(argv[1]);
    DIE(server->portno == 0, "atoi problem");

    // initializam socketii disponibili ai clientilor tcp si udp;
    // AF_INET -- IPv4
    // INADDR_ANY -- acceptam orice mesaj primit (de la orice adresa)
    memset((char *)&(server->tcp_addr), 0, sizeof(server->tcp_addr));
    server->tcp_addr.sin_family = AF_INET;
    server->tcp_addr.sin_port = htons(server->portno);
    server->tcp_addr.sin_addr.s_addr = INADDR_ANY;

    memset((char *) &(server->udp_addr), 0, sizeof(server->udp_addr));
    server->udp_addr.sin_family = PF_INET;
    server->udp_addr.sin_port = htons(server->portno);
    server->udp_addr.sin_addr.s_addr = INADDR_ANY;

    // Facem bind atat pe o directie (client tcp), cat si pe cealalta (client udp)
    server->connection_ret = bind(server->sockfd_tcp, (struct sockaddr *) &(server->tcp_addr), sizeof(struct sockaddr));
    DIE(server->connection_ret < 0, "tcp bind");

    server->connection_ret = bind(server->sockfd_udp, (struct sockaddr *) &(server->udp_addr), sizeof(struct sockaddr));
    DIE(server->connection_ret < 0, "udp bind");

    // pentru tcp, avem nevoie si de un accept, prin urmare, mai apare
    // si un listen suplimentar; pentru udp nu este nevoie, intrucat udp
    // nu depinde de acceptul serverului
    server->connection_ret = listen(server->sockfd_tcp, MAX_CLIENTS);
    DIE(server->connection_ret < 0, "tcp listen");

    // Adaugam in setul de descriptori:
    // -- STDIN_FILENO, adica 0, adica socketul destinat citirii de la tastatura
    // -- sockfd_tcp, se vede clar cine a fost introdus
    // -- sockfd_udp, la fel, stim cine e
    FD_SET(STDIN_FILENO, &(server->read_fds));
    FD_SET(server->sockfd_tcp, &(server->read_fds));
    FD_SET(server->sockfd_udp, &(server->read_fds));

    // aici atribuim lui fdmax valoarea maxima intre cei doi socketi, pentru ca,
    // in sine, fdmax trebuie sa retina cel mai mare socket din setul dat
    if (server->sockfd_tcp < server->sockfd_udp) {
        server->fdmax = server->sockfd_udp;
    }
    else {
        server->fdmax = server->sockfd_tcp;
    }
}

// functie care extrage din mesajul primit de la udp sub forma de char *, un
// intreg pe 4 octeti
uint32_t get_int_value(char* buffer) {
    int buff1 = 0, buff2 = 0, buff3 = 0, buff4 = 0;
    buff1 = (int8_t)buffer[TOPIC_LEN + 1];
    buff1 <<= 24;
    buff1 = htonl(buff1);
    buff2 = (int8_t)buffer[TOPIC_LEN + 2];
    buff2 <<= 16;
    buff2 = htonl(buff2);
    memset(&buff2, 0, 1);
    buff3 = (int8_t)buffer[TOPIC_LEN + 3];
    buff3 <<= 8;
    buff3 = htonl(buff3);
    memset(&buff3, 0, 2);
    buff4 = (int8_t)buffer[TOPIC_LEN + 4];
    buff4 = htonl(buff4);                        
    memset(&buff4, 0, 3);
    return htonl(buff1 + buff2 + buff3 + buff4);
}

// functie care extrage din mesajul primit de la udp sub forma de char *, un
// intreg pe 2 octeti
uint16_t get_short_real_value(char* buffer) {
    uint16_t buff1 = 0, buff2 = 0;
    buff1 = (int8_t)buffer[TOPIC_LEN];
    buff1 <<= 8;
    buff1 = htons(buff1);
    buff2 = (int8_t)buffer[TOPIC_LEN + 1];
    buff2 = htons(buff2);
    memset(&buff2, 0, 1);
    return htons(buff1 + buff2);
}

// functie care prelucreasa comenzile primite de la tastatura; singura comanda
// pe care o putem primi in server este cea de exit, care deconecteaza automat
// si clientii conectati deja la server
void stdin_commands(struct Server* server, struct msg2subscr to_send) { 
    memset(buffer, 0, strlen(buffer));
    server->n = scanf("%s", buffer);
    DIE(server->n < 0, "read error");
    if (strncmp(buffer, "exit", 4) == 0) {
        for (int j = 0; j < server->clients.size(); j++) {
            memset(&to_send, 0, sizeof(to_send));
            memset(to_send.command, 0, 25);
            memcpy(to_send.command, "exit", 4);
            send(server->clients[j]->client_sockfd, &to_send, sizeof(to_send), 0);
            close(server->clients[j]->client_sockfd);
        }
        exit(0);
    }
    else {
        DIE(strncmp(buffer, "exit", 4) != 0, "no such command");
    }
}

// functie in care verificam, in cazul in care avem clienti tcp abonati la topicul
// primit de la udp cu sf = 1, dar ei sunt offline, iar daca sunt, atunci ii
// vom baga mesajele intr-o coada, pentru a le trimite in momentul in care se
// reconecteaza
void offline_msg_update(struct Server* server, struct msg2subscr to_send) {
    // cautam topicul; verificam daca abonatii sunt activi
    for (j = 0; j < server->sf_topics.size(); j++) {
        if (strcmp(server->sf_topics[j]->topic, to_send.topic) == 0) {
            for (k = 0; k < server->sf_topics[j]->subscripted_clients.size(); k++) {
                server->ok = 1;
                for (t = 0; t < server->clients.size(); t++) {
                    if (server->sf_topics[j]->subscripted_clients[k].
                            compare(string(server->clients[t]->ID)) == 0) {
                        server->ok = 0;
                        break;
                    }
                }
                if (server->ok) {
                    break;
                }  
            }
            // daca clientul nu este online, desi e abonat la topic, adaugam
            // mesajul intr-o coada
            if(server->ok) {
                struct msg2subscr* copy = 
                    (struct msg2subscr* )malloc(sizeof(struct msg2subscr));
                memcpy(copy, &to_send, sizeof(copy));
                memcpy(copy->topic, to_send.topic, TOPIC_LEN);
                copy->addr = to_send.addr;
                copy->exponent = to_send.exponent;
                copy->int_value = to_send.int_value;
                copy->mantisa = to_send.mantisa;
                copy->port = to_send.port;
                copy->short_real_value = to_send.short_real_value;
                copy->sign = to_send.sign;
                memcpy(copy->string_value, 
                    to_send.string_value, BUFLEN - TOPIC_LEN - 1);
                copy->type = to_send.type;
                server->sf_queue.push_back(copy);
            }
            server->ok = 0;
            break;
        }    
    }
}

// in momentul in care se primeste un mesaj de la udp, serverul va trimite
// clientilor tcp abonati la respectivul topic datele necesare
void send_topic_to_tcp_clients(struct Server* server, struct msg2subscr to_send) {
    // se trimite mesajul clientilor activi care sunt abonati la topic cu sf = 0
    for (j = 0; j < server->no_sf_topics.size(); j++) {
        if (strcmp(to_send.topic, server->no_sf_topics[j]->topic) == 0) {
            k = 0;
            for (; k < server->no_sf_topics[j]->subscripted_clients.size(); k++) {
                for (t = 0; t < server->clients.size(); t++) {
                    if (server->no_sf_topics[j]->subscripted_clients[k].
                            compare(string(server->clients[t]->ID)) == 0) {
                        send(server->clients[t]->client_sockfd, &to_send,
                            sizeof(to_send), 0);
                    }
                }
            }
            break;
        }   
    }
    // se trimite mesajul clientilor activi care sunt abonati la topic cu sf = 1
    for (j = 0; j < server->sf_topics.size(); j++) {
        if (strcmp(to_send.topic, server->sf_topics[j]->topic) == 0) {
            k = 0;
            for (; k < server->sf_topics[j]->subscripted_clients.size(); k++) {
                for (t = 0; t < server->clients.size(); t++) {
                    if (server->sf_topics[j]->subscripted_clients[k].
                            compare(string(server->clients[t]->ID)) == 0) {
                        send(server->clients[t]->client_sockfd, &to_send,
                            sizeof(to_send), 0);
                    }
                }
            }
            break;
        }   
    }
}

// se primeste un mesaj de la un client tcp; se vor face prelucrari pe mesaj,
// iar apoi se va crea un mesaj ce trebuie trimis catre un subscriber (client
// tcp) urmand ca apoi sa si fie trimis 
void check_if_udp_msg(struct Server* server, struct msg2subscr to_send) {
    memset(buffer, 0, BUFLEN);
    struct sockaddr_in from_station;
    socklen_t from_station_len = sizeof(from_station);
    server->connection_ret = recvfrom(i, buffer, BUFLEN, 0, 
        (struct sockaddr *)&from_station, &from_station_len);
    memset(&to_send, 0, sizeof(to_send));
    // luam portul si ip-ul clientului udp, precum si topicul
    to_send.port = from_station.sin_port;
    to_send.addr = from_station.sin_addr;
    memset(to_send.topic, 0, TOPIC_LEN);
    memcpy(to_send.topic, buffer, TOPIC_LEN - 1);
    to_send.type = buffer[TOPIC_LEN - 1];
    // preluam datele in functie de tipul acestora
    if (to_send.type == 0) {
        to_send.sign = buffer[TOPIC_LEN];
        to_send.int_value = get_int_value(buffer);
    }
    else if (to_send.type == 1){
        to_send.short_real_value = get_short_real_value(buffer);
    }
    else if (to_send.type == 2) {
        to_send.sign = buffer[TOPIC_LEN];
        to_send.exponent = get_int_value(buffer);
        to_send.mantisa = buffer[TOPIC_LEN + 5];
    }
    else {
        memcpy(to_send.string_value, buffer + TOPIC_LEN, BUFLEN - TOPIC_LEN - 1);
    }
    // daca sunt clienti ce trebuie sa primeasca mesajul, dar sunt offline, si
    // in plus mesajul este cu sf = 1
    offline_msg_update(server, to_send);
    // trimite mesajul pe topicul respectiv catre clientii tcp
    send_topic_to_tcp_clients(server, to_send);
    
}

// functie care face conexiunea cu un client tcp, cand acesta cere; daca clientul
// este deja conectat (sau se afla conectat un client cu acelasi id), se va
// afisa un mesaj corespunzator; in cazul in care sunt de trimis mesaje cu sf=1
// unui client care a fost deconectat intre timp, se vor trimite
void send_tcp_msg(struct Server* server, struct msg2subscr to_send) {
    server->clilen = sizeof(server->tcp_addr);
    // acceptarea cererii de conexiune a clientului
    server->newsockfd = accept(server->sockfd_tcp, 
        (struct sockaddr *) &(server->tcp_addr), &(server->clilen));
    DIE(server->newsockfd < 0, "accept");
    memset(buffer, 0, BUFLEN);
    // se primeste id-ul clientului
    recv(server->newsockfd, buffer, sizeof(buffer), 0);
    // daca clientul este deja conectat, afiseaza un mesaj corespunzator
    for(j = 0; j < server->clients.size(); j++) {
        if(strcmp(server->clients[j]->ID, buffer) == 0) {
            printf("Client %s already connected.\n", buffer);
            server->ok = 1;
            break;
        }
    }
    // daca clientul este deja conectat, trimite un mesaj clientului care
    // incearca sa se conecteze cu acelasi id, pentru a se deconecta
    if(server->ok == 1) {
        memset(&to_send, 0, sizeof(to_send));
        memset(to_send.command, 0, 25);
        memcpy(to_send.command, "duplicate", 9);
        send(server->newsockfd, &to_send, sizeof(to_send), 0);
        server->ok = 0;
        return;
    }

    // daca clientul nu este in lista de clienti activi
    FD_SET(server->newsockfd, &(server->read_fds));
    if (server->newsockfd > server->fdmax) { 
        server->fdmax = server->newsockfd;
    }
    struct Client* new_client = (struct Client*)malloc(sizeof(struct Client));
    new_client->client_sockfd = server->newsockfd;
    memcpy(new_client->ID, buffer, strlen(buffer));
    
    if(j == server->clients.size()) {
        server->clients.push_back(new_client);
        printf("New client %s connected from %s:%d.\n", buffer,
            inet_ntoa(server->tcp_addr.sin_addr), server->tcp_addr.sin_port);
        // verificam daca sunt mesaje de trimis clientului, cat timp acesta a
        // fost deconectat; topicurile la care s-a abonat trebuie sa fie cu sf=1
        for (j = 0; j < server->sf_queue.size(); j++) {
            server->ok = 0;
            for(k = 0; k < server->sf_topics.size(); k++) {
                if (strcmp(server->sf_topics[k]->topic,
                        server->sf_queue[j]->topic) == 0) {   
                    t = 0;   
                    for (; t < server->sf_topics[k]->subscripted_clients.size(); t++){
                        if (server->sf_topics[k]->subscripted_clients[t].
                                compare(string(new_client->ID)) == 0) {
                            send(new_client->client_sockfd, server->sf_queue[j],
                                sizeof(to_send), 0);
                            server->ok = 1;
                        }
                    }
                    if (server->ok) {
                        server->sf_queue.erase(server->sf_queue.begin() + j);
                        j--;
                        break;
                    }
                }
            }
        }
    }
}

// functie care se apeleaza in momentul in care un client se deconecteaza
void client_disconnects(struct Server* server) {
    memset(buffer, 0, BUFLEN);
    memcpy(buffer, "exit", 4);
    
    for(j = 0; j < server->clients.size(); j++) {
        if(server->clients[j]->client_sockfd == i) {
            break;
        }
    }
    printf("Client %s disconnected.\n", server->clients[j]->ID);
    server->clients.erase(server->clients.begin() + j);
    close(i);
    FD_CLR(i, &(server->read_fds));
}

// functie care adauga topicurile cu sf=0 in lista de topicuri cu sf=0; daca
// topicurile deja exista, atunci nu va trebui decat sa adaugam clientul
void no_SF_subscribe(struct Server* server, struct msg2srv received_msg) {
    for (j = 0; j < server->no_sf_topics.size(); j++) {
        if (strcmp(server->no_sf_topics[j]->topic, received_msg.topic) == 0) {
            break;
        }
    }
    // nu exista topicul deja in lista de topicuri fara SF
    if (j == server->no_sf_topics.size()) {
        struct Subscription_no_SF* new_subscription = (struct 
            Subscription_no_SF *)malloc(sizeof(struct Subscription_no_SF));
        strcpy(new_subscription->topic, received_msg.topic);
        for (j = 0; j < server->clients.size(); j++) {
            if (server->clients[j]->client_sockfd == i) {
                break;
            }
        }
        new_subscription->subscripted_clients.
            push_back(string(server->clients[j]->ID));
        server->no_sf_topics.push_back(new_subscription);                                    
    }
    // exista topicul, ramane doar sa adaugam clientul
    else {
        for (k = 0; k < server->clients.size(); k++) {
            if (server->clients[k]->client_sockfd == i) {
                break;
            }
        }
        server->no_sf_topics[j]->subscripted_clients.
            push_back(string(server->clients[k]->ID));
    }
}

// functie care adauga topicurile cu sf=1 in lista de topicuri cu sf=1; daca
// topicurile deja exista, atunci nu va trebui decat sa adaugam clientul
void SF_subscribe(struct Server* server, struct msg2srv received_msg) {
    for (j = 0; j < server->sf_topics.size(); j++) {
        if (strcmp(server->sf_topics[j]->topic, received_msg.topic) == 0) {
            break;
        }
    }
    // nu exista topicul deja in lista de topicuri cu SF
    if (j == server->sf_topics.size()) {
        struct Subscription_SF* new_subscription = 
            (struct Subscription_SF *)malloc(sizeof(struct Subscription_SF));
        strcpy(new_subscription->topic, received_msg.topic);
        for (j = 0; j < server->clients.size(); j++) {
            if (server->clients[j]->client_sockfd == i) {
                break;
            }
        }
        new_subscription->subscripted_clients.
            push_back(string(server->clients[j]->ID));
        server->sf_topics.push_back(new_subscription);                                    
    }
    // exista topicul, ramane doar sa adaugam clientul
    else {
        for (k = 0; k < server->clients.size(); k++) {
            if (server->clients[k]->client_sockfd == i) {
                break;
            }
        }
        server->sf_topics[j]->subscripted_clients.
            push_back(string(server->clients[k]->ID));
    }
}

// functie prin care un client se va abona la un topic, in functie de cerinta
void subscribe_to_topic(struct Server* server, struct msg2srv received_msg) {
    if (received_msg.SF == 0){
        no_SF_subscribe(server, received_msg);
    }
    else if (received_msg.SF == 1) {
        SF_subscribe(server, received_msg);
    }
}

// dezabonare de la un topic de tipul sf=0
void no_SF_unsubscribe(struct Server* server, struct msg2srv received_msg) {
    for (k = 0; k < server->no_sf_topics[j]->subscripted_clients.size(); k++) {
        for (t = 0; t < server->clients.size(); t++) {
            if (server->no_sf_topics[j]->subscripted_clients[k].
                compare(string(server->clients[t]->ID)) == 0) {
                break;
            }
        }
        server->no_sf_topics[j]->subscripted_clients.
            erase(server->no_sf_topics[j]->subscripted_clients.begin() + k);
    }
}

// dezabonare de la un topic de tipul sf=1
void SF_unsubscribe(struct Server* server, struct msg2srv received_msg) {
    for(j = 0; j < server->sf_topics.size(); j++) {
        if(strcmp(received_msg.topic, server->sf_topics[j]->topic) == 0) {
            break;
        }
    }
    for (k = 0; k < server->sf_topics[j]->subscripted_clients.size(); k++) {
        for (t = 0; t < server->clients.size(); t++) {
            if (server->sf_topics[j]->subscripted_clients[k].
                compare(string(server->clients[t]->ID)) == 0) {
                break;
            }
        }
        server->sf_topics[j]->subscripted_clients.
            erase(server->sf_topics[j]->subscripted_clients.begin() + k);
    }
}

// functie de dezabonare a unui client tcp de la un topic
void unsubscribe_to_topic(struct Server* server, struct msg2srv received_msg) {
    for(j = 0; j < server->no_sf_topics.size(); j++) {
        if(strcmp(received_msg.topic, server->no_sf_topics[j]->topic) == 0) {
            break;
        }
    }
    if (j != server->no_sf_topics.size()) {
        no_SF_unsubscribe(server, received_msg);
    }
    else {
        SF_unsubscribe(server, received_msg);
    }
}

// functie primeste un mesaj de la client tcp; poate fi doar subscribe la un
// topic, sau unsubscribe
void process_received_msg(struct Server* server, struct msg2srv received_msg) {
    if (strncmp(received_msg.action, "subscribe", 9) == 0) {
        subscribe_to_topic( server, received_msg);
    }
    else if (strncmp(received_msg.action, "unsubscribe", 11) == 0) {
        unsubscribe_to_topic(server, received_msg);
    }
}

void recv_tcp_msg(struct Server* server, struct msg2srv received_msg) {
    memset(&received_msg, 0, sizeof(received_msg));
    server->n = recv(i, &received_msg, sizeof(received_msg), 0);
    DIE(server->n < 0, "recv");
    // daca nu se primeste nimic ca si mesaj, atunci clientul se deconecteaza
    if (server->n == 0) {
        client_disconnects(server);
    }
    // daca se primeste un mesaj, atunci verificam si prelucram datele extrase
    // din respectivul mesaj
    else {
        process_received_msg(server, received_msg);
    }
}

void is_set(struct Server* server, struct msg2subscr to_send, 
    struct msg2srv received_msg) {
    // este un file descriptor din setul nostru? DA
    if (FD_ISSET(i, &(server->tmp_fds))) {
        // la inceput, verificam daca se primeste ceva de la
        // tastatura, file descriptor specific STDIN_FILENO
        if (i == STDIN_FILENO) {
            stdin_commands(server, to_send);
        }
        // se primeste un mesaj de client udp
        else if (i == server->sockfd_udp) {
            check_if_udp_msg(server, to_send);
        }
        else if (i == server->sockfd_tcp) {
            send_tcp_msg(server, to_send);
        }
        // se primesc date de la ceilalti clienti, deja logati
        else {
            recv_tcp_msg(server, received_msg);
        }
    }
}

void run_server(struct Server* server) {
    struct msg2subscr to_send;
    struct msg2srv received_msg;
    while(1) {
        //facem o copie a lui read_fds, pentru a predispune setul la
        // diverse posibile modificari
        server->tmp_fds = server->read_fds;
        
        // element cheie pentru multiplexare I/O
        server->connection_ret = select(server->fdmax + 1,
            &(server->tmp_fds), NULL, NULL, NULL);
        DIE(server->connection_ret < 0, "select");

        // teoretic parcurgem setul prin socketi; din pacate, vom verifica
        // toate numerele, chiar daca unora nu li sunt asociate niste socketi
        for (i = 0; i <= server->fdmax; i++) {
            is_set(server, to_send, received_msg);
        }
    }
}

int main (int argc, char* argv[]) {
    
    struct Server* server = (struct Server *)malloc(sizeof(struct Server));
    // intializam
    init(server, argc, argv);
    // si rulam :)
    run_server(server);

    return 0;
}
