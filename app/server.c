#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>


#define MAX_LINES 104
#define GET_URI_PARAMS 10
#define MAX_BYTES 8192
 
static struct option long_options[] = {
    {"directory", required_argument, NULL, 'd'},
    {NULL, 0, NULL, 0}
};

int recv_from_client(int client_conn_fd,char *buf) {

	int recv_bytes = recv(client_conn_fd,buf,MAX_BYTES,0);
	if(recv_bytes == -1) {
		fprintf(stdout,"Error %d %s %s\n",__LINE__,strerror(errno),__FUNCTION__);
		return 1;
	}
	if(recv_bytes == 0) {
		printf("Connection closed: %s %s\n", strerror(errno),__FUNCTION__);
	}
	return 0;
}

void parse_into_lines(char **lines,int *line, char *buf) {

	char delim[] = "\n";
	lines[*line] = strtok(buf,delim);


	while(lines[*line] != NULL) {
		//printf("Line read %d %s\n",*line,lines[*line]);
		++*line;
		lines[*line] = strtok(NULL,delim);
	}
}

void create_custome_response(char *str,char *buf) {
	int len = strlen(str);

	int success_code = 200;
	sprintf(buf,"HTTP/1.1 %d OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",success_code,len,str);
}

void remove_crlf_from_end(char *src,char *dst ) {
	
	while(*src != '\r') {
		if(isspace(*src)){
			++src;
			continue;
		}
		*dst = *src;
		dst++;
		src++;
	}
}

void handle_user_agent_query(char** lines,int maxlines,char* buf) {
	for(int i = 1 ; i < maxlines; i++) {
		if(strcasestr(lines[i],"User-agent") != NULL) {
			printf("User-agent found\n");
			char dup_str[MAX_BYTES];
			strcpy(dup_str,lines[i]);
			char *user_agent_val = strtok(dup_str,":");
		
			if(user_agent_val != NULL) {					
				user_agent_val = strtok(NULL,":");
				printf("User-agent string found %s\n",user_agent_val);
				char newstr[MAX_BYTES] = {'\0'};
				remove_crlf_from_end(user_agent_val,newstr);
				create_custome_response(newstr,buf);
				printf("Buffer populated %s with user-agent %s\n",buf,user_agent_val);			
			}
		}
	}
}

void handle_file_request(char *file, char *buf) {
	struct stat st;
	int success_code = 404;
	int input_fd;

	if(stat(file,&st) == -1) {
		sprintf(buf,"HTTP/1.1 %d Not Found\r\n\r\n",success_code);
	} else {
		success_code = 200;	
		
		int bytes_written = sprintf(buf,"HTTP/1.1 %d OK\r\nContent-Type: application/octet-stream\r\n",success_code);
		input_fd = open(file, O_RDONLY);
		int sz = lseek(input_fd,0L,SEEK_END);
		lseek(input_fd,0L,SEEK_SET);
	
		
		int old = bytes_written;
		bytes_written = sprintf(buf+old,"Content-Length: %d\r\n\r\n",sz);
		
		
		old = old + bytes_written;
		char buf_read[1024] = {0};
		int read_bytes =  read(input_fd,buf_read,1024);
		if(read_bytes == -1) {
			printf("Error in reading %s \n",strerror(errno));
			exit(EXIT_FAILURE);
		}
		while(read_bytes != 0) {
			if(read_bytes == -1) {
				printf("Error in reading %s \n",strerror(errno));
				exit(EXIT_FAILURE);
			}

			bytes_written = sprintf(buf+old,"%s",buf_read);
			old = old +bytes_written;
			read_bytes =  read(input_fd,buf_read,1024);
			if(read_bytes == -1) {
				printf("Error in reading %s \n",strerror(errno));
			}
		}
	}
	printf("Final buf %s\n",buf);
}

void create_response_for_endpoint(char *uri,char** lines,int maxlines,char *buf){
	struct stat st;

	char temp_buf[MAX_BYTES];
	int success_code = 200;

	char delim[] = "/";
	char *tokens[GET_URI_PARAMS];

	int i = 0;
	tokens[i] = strtok(uri,delim);

	while(tokens[i] != NULL) {
		i++;
		tokens[i] = strtok(NULL,delim);
	}

	if(i == 2 && strcmp(tokens[i-2],"echo") == 0 ) {
		create_custome_response(tokens[i-1],buf);
		return;
	}
    
	if(i >= 1 && strcasecmp(tokens[i-1],"User-agent") == 0) {	
		printf("User-agent searched\n");
		handle_user_agent_query(lines,maxlines,buf);
		return;
	
	}

	if(i == 2 && strcmp(tokens[i-2],"files") == 0) {
		printf("Query for files\n");
		handle_file_request(tokens[i-1],buf);
		return;
	}

	if(tokens[0] != NULL ) {
		if(stat(tokens[0],&st) == -1) {
			printf("Stat read %s %s\n",strerror(errno),__FUNCTION__);
			success_code = 404;
			sprintf(buf,"HTTP/1.1 %d Not Found\r\n\r\n",success_code);
		} else {
			sprintf(buf,"HTTP/1.1 %d OK\r\n\r\n",success_code);
		}
	} 
	if(i == 0 && tokens[0] == NULL){
		//Empty directry 
		sprintf(buf,"HTTP/1.1 %d OK\r\n\r\n",success_code);
	} 
	
}

void* handle_client_conn(void *args) {
	
	int *pclient_conn_fd = (int*)args;
	int client_conn_fd = *pclient_conn_fd;

	char buf_recv[MAX_BYTES];
	if(recv_from_client(client_conn_fd,buf_recv) == 1) {
		printf("Exiting error in recv\n");
		exit(EXIT_FAILURE);
	}

	printf("Recv bytes %s\n",buf_recv);
	printf("\n");

	char* lines[MAX_LINES];
	int line = 0;
	parse_into_lines(lines,&line,buf_recv);

	char method[MAX_BYTES],uri[MAX_BYTES];
	sscanf(lines[0],"%s %s",method,uri);
	printf("Method %s Uri %s \n",method,uri);

	char buf_send[MAX_BYTES] = {0};
	create_response_for_endpoint(uri,lines,line,buf_send);
	send(client_conn_fd,buf_send,MAX_BYTES,0);

	
	return NULL;
}
int main(int argc, char *argv[] ) {
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage
	//
	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	int client_conn_fd;
	int ch;
	char *diret_name = NULL;

	while ((ch = getopt_long(argc, argv, "d:", long_options, NULL)) != -1){
	switch (ch){
        case 'd':
            diret_name = optarg; 
        break;
    	}
    }

	if(diret_name != NULL) {
		printf("Directory change is requested\n");
		if(chdir(diret_name) != 0) {
			printf("Dirctory change not happened %s\n",strerror(errno));
			return 1;
		}
	}
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

	while(1) {

		client_conn_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
		printf("Client connected\n");
		pthread_t thread_id;
		pthread_create(&thread_id,NULL,handle_client_conn,(void*)&client_conn_fd);
	}	
	
	close(server_fd);

	return 0;
}
