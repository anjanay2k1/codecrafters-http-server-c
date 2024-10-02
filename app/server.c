#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h>           
#include <sys/stat.h>


#define MAX_LINES 104
#define MAX_BYTES 8192

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage
	//
	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	//
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
	 	printf("Socket creation failed: %s...\n", strerror(errno));
	 	return 1;
	}
	//
	// // Since the tester restarts your program quite often, setting SO_REUSEADDR
	// // ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
	 	printf("SO_REUSEADDR failed: %s \n", strerror(errno));
	 	return 1;
	}
	//
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
	 								 .sin_port = htons(4221),
	 								 .sin_addr = { htonl(INADDR_ANY) },
	 								};
	//
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
	 	printf("Bind failed: %s \n", strerror(errno));
	 	return 1;
	 }
	//
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
	 	printf("Listen failed: %s \n", strerror(errno));
	 	return 1;
	}
	//
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);
	//
	int client_conn_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
	printf("Client connected\n");

	char buf_recv[MAX_BYTES];
	int recv_bytes = recv(client_conn_fd,buf_recv,MAX_BYTES,0);
	if(recv_bytes == 0) {
		printf("Connection closed: %s %s\n", strerror(errno),__FUNCTION__);
	}
	printf("Recv bytes\n %s",buf_recv);
	printf("\n");

	char* lines[MAX_LINES];
	int i = 0;
	char delim[] = "\n";
	lines[i] = strtok(buf_recv,delim);


	while(lines[i] != NULL) {
		printf("Line read %d %s\n",i,lines[i]);
		++i;
		lines[i] = strtok(NULL,delim);
	}

	char method[MAX_BYTES],uri[MAX_BYTES];
	sscanf(lines[0],"%s %s",method,uri);
	printf("Method %s Uri %s \n",method,uri);

	struct stat st;
	char buf_send[MAX_BYTES];
	int success_code = 200;

	if(stat(uri,&st) == -1) {
		printf("Stat read %s %s\n",strerror(errno),__FUNCTION__);
		success_code = 404;
		sprintf(buf_send,"HTTP/1.1 %d Not Found\r\n\r\n",success_code);
	} else {
		sprintf(buf_send,"HTTP/1.1 %d OK\r\n\r\n",success_code);
	}
	printf("buf %s \n",buf_send);
	send(client_conn_fd,buf_send,sizeof(buf_send),0);
	close(server_fd);

	return 0;
}
