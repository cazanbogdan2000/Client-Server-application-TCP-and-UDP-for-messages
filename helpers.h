#ifndef _HELPERS_H
#define _HELPERS_H 1

#include <stdio.h>
#include <stdlib.h>

#define BUFLEN		1552	// dimensiunea maxima a calupului de date
#define MAX_CLIENTS	5	   // numarul maxim de clienti in asteptare
#define TOPIC_LEN 51

char buffer[BUFLEN];

struct msg2srv {
	
	char action[13]; // subscribe sau unsubscribe; pentru exit, nu se trimite nicio lungime
	char topic[TOPIC_LEN]; // topicul la care se aboneaza / dezaboneaza clientul
	bool SF; // store and forward; poate fi 0 sau 1; nu conteaza pentru action == 1
}Message_to_Server;

struct msg2subscr {
	char command[12];
	int port;
	in_addr addr;
	char topic[TOPIC_LEN];
	unsigned int type;
	unsigned char sign;
	uint32_t int_value;
	uint16_t short_real_value;
	uint8_t mantisa;
	uint32_t exponent;
	char string_value[BUFLEN - TOPIC_LEN];
	
}Message_to_Subscriber;


/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)


#endif
