#include "server_listen.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "config.h"
#include "server.h"
#include "server_cllist.h"
#include "server_accept.h"
#include "request.h"
#include "connection.h"

pthread_t listener;

void handle_pong(int client_id) {
	clients[client_id].cl_inactivity = 0;
}

void handle_response(int client_id, struct socket_message *sm) {
	struct response_t resp;
	if (sm->length != 1 + sizeof(struct response_t)) {
		fprintf(stderr, "Invalid response: length\n");
		return;
	}
	
	memcpy(&resp, &sm->buffer[1], sizeof(struct response_t));
	
	printf("\rout[%d]: = %lf (from %s)\n", resp.id, resp.result, clients[client_id].cl_name);
	print_prompt();
}

void *thread_listen(void *args) {
	while (1) {
		pthread_testcancel();
		
		struct epoll_event event;
		int waiting = epoll_wait(waiting_poll, &event, 1, -1);
		if (waiting < 0) {
			perror("Cannot epoll wait");
			sleep(1);
			continue;
		}
		
		if (waiting == 0) {
			// no one is waiting
			continue;
		}
		
		int sock = event.data.fd;
		
		struct socket_message sm;
		if (receive_sm(sock, &sm) != 0) {
			sleep(1);
			continue;
		}
		char msg_type = sm.buffer[0];
		
#ifdef CONF_DATAGRAM
		int client_id = client_for_addr(&sm.addr.saddr, sm.addr.len);
#else
		int client_id = client_for_sock(sock);
#endif
		
		if (client_id < 0 && msg_type != MSG_TYPE_REGISTER && msg_type != MSG_TYPE_UNREGISTER) {
			printf("\rReceived a message from an unknown client\n");
			print_prompt();
			continue;
		}
		
		switch (msg_type) {
		case MSG_TYPE_REGISTER:
			// it only works on UDP
			register_client(sock, &sm);
			break;
			
		case MSG_TYPE_UNREGISTER:
			unregister_client(client_id);
			break;
			
		case MSG_TYPE_PONG:
			handle_pong(client_id);
			break;
			
		case MSG_TYPE_RESPONSE:
			handle_response(client_id, &sm);
			break;
			
		default:
			fprintf(stderr, "Received invalid message type: %d\n", (int) msg_type);
			continue;
		}
	}
}

void setup_listener(void) {
	if (pthread_create(&listener, NULL, thread_listen, NULL) != 0) {
		perror("Cannot create listener thread");
		exit(-1);
	}
	
#ifdef CONF_DATAGRAM
	// we need to monitor two sockets
	// in TCP we monitor sockets after accepting
	add_to_poll(sock_local);
	add_to_poll(sock_net);
#endif
}

void cancel_listener(void) {
	pthread_cancel(listener);
	struct timespec ts;
	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	pthread_timedjoin_np(listener, NULL, &ts);
}
