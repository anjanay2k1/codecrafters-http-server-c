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
#include <zlib.h>
#include <pthread.h>


#define MAX_LINES 101
#define GET_URI_PARAMS 10
#define MAX_BYTES 8192
#define MAX_COMPRESSION_OPTIONS 10


static struct option long_options[] = {
    {"directory", required_argument, NULL, 'd'},
    {NULL, 0, NULL, 0}
};

static char* server_compression_otion[] = {
	"gzip"
};

static void remove_crlf_from_end(char *src,char *dst ) {
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
void gzip_compression(char *src,char *dst,int *dst_len) {
	
	z_stream defstream;
    defstream.zalloc = Z_NULL;
    defstream.zfree = Z_NULL;
    defstream.opaque = Z_NULL;

	defstream.avail_in = strlen(src); 
    defstream.next_in = (Bytef *)src; 
    defstream.avail_out = 8192; // size of output
    defstream.next_out = (Bytef *)dst; // output char array
	
	deflateInit2(&defstream, Z_BEST_COMPRESSION,Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    deflate(&defstream, Z_FINISH);
    deflateEnd(&defstream);
	*dst_len = defstream.total_out;
	printf("Compressed byte len %lu\n",defstream.total_out);

}
int search_in_server_supportd_compresion(char *str_to_find) {
	int ret = 1;
	int max_option = sizeof(server_compression_otion)/sizeof(server_compression_otion[0]);
	for(int i = 0 ; i < max_option ; i++) {
		if(strcasecmp(server_compression_otion[i],str_to_find) == 0) {
			printf("Server compression option found %s searched %s\n",server_compression_otion[i],
							str_to_find);
			ret = 0;
		}
	}
	return ret;
}

void parse_req_into_lines(char **lines,int *line, char *buf) {

	char delim[] = "\n";
	lines[*line] = strtok(buf,delim);


	while(lines[*line] != NULL) {
		//printf("Line read %d %s\n",*line,lines[*line]);
		++*line;
		lines[*line] = strtok(NULL,delim);
	}
}

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

int find_string_in_parsed_header(char* str_to_find,char **lines,int maxline,char *found,char** p_found) {
	for(int i = 1 ; i < maxline; i++) {
		if(strcasestr(lines[i], str_to_find) != NULL) {
			
			printf("Search string found %s \n",str_to_find);
			char dup_str[MAX_BYTES];
			strcpy(dup_str,lines[i]);
			char *str_to_find_val = strtok(dup_str,":");
		
			if(str_to_find_val != NULL) {					
				str_to_find_val = strtok(NULL,":");
				printf("Search string value %s\n",str_to_find_val);
				
				//Here the value can be comma seperate list also .need to further parse this 
				int j = 0;
				p_found[j] = strtok(str_to_find_val,",");
				while(p_found[j] != NULL) {
					printf("Comm seperated values[%d] %s\n",j,p_found[j]);
					j++;
					p_found[j] = strtok(NULL,",");

					if(p_found[j] == NULL && j == 1) {
						printf("Only single value is given for the searched string\n");
						remove_crlf_from_end(str_to_find_val,found);
						p_found[0] = NULL;
						return 0;
					}
				}
				return 0;			
			}
		}
	}

	return 1;
}

int handle_user_agent_query(char** lines,int maxlines,char* found) {
	
	char  user_agent_single_val[MAX_BYTES] = {'\0'};
	char* user_agent_multiple_val[MAX_COMPRESSION_OPTIONS] = {0};

	if( find_string_in_parsed_header("User-agent",lines,maxlines,user_agent_single_val,user_agent_multiple_val) == 0 ){
		//Here in all case this parameter will return single value
		printf("User-agent value %s\n",user_agent_single_val);
		memcpy(found,user_agent_single_val,strlen(user_agent_single_val));
		return 1;
	}
	return 0;
}
	
int handle_file_get_content_request(char *file, char *buf,int *len) {
	struct stat st;
	int input_fd;

	if(stat(file,&st) == -1) {
		return 1;

	} else {
		
		input_fd = open(file, O_RDONLY);
		int sz = lseek(input_fd,0L,SEEK_END);
		lseek(input_fd,0L,SEEK_SET);
		*len = sz;
		char buf_read[1024] = {0};
		int prev_read = 0;
		int read_bytes =  read(input_fd,buf_read,1024);
		if(read_bytes == -1) {
			printf("Error in reading %s \n",strerror(errno));
			return 1;
		}
		memcpy(buf+prev_read,buf_read,read_bytes);
		prev_read += read_bytes;

		while(read_bytes != 0) {
			
			read_bytes =  read(input_fd,buf_read,1024);
			if(read_bytes == -1) {
				printf("Error in reading %s \n",strerror(errno));
				return 1;
			}
			memcpy(buf+prev_read,buf_read,read_bytes);
		}

		return 0;
	}
	return 1;
}
void handle_file_post_request(char* file,char **lines,int maxlines,char *buf) {

	int output_fd;
	int success_code = 201;
	

	int content_len = -1;
	char newstr[MAX_BYTES] = {'\0'};
	char* comma_sep_vals[10];
	if(find_string_in_parsed_header("Content-Length",lines,maxlines,newstr,comma_sep_vals) == 0) {
		content_len = atoi(newstr);
	}

	
	int i = 0;
	for(i = 1 ; i < maxlines; i++) {
		if(strcasestr(lines[i],"\r") == NULL) {
			break;
		}
	}
    
	//next line is body
	if ( i > maxlines) {
		printf("I values exceeds %d\n",i);
		exit(EXIT_FAILURE);
	}

	if(content_len == -1) {
		printf("No content len field \n");
		exit(EXIT_FAILURE);
	}
    
	printf("Here is Response body %s \n",lines[i]);
	printf("File to create %s\n",file);

	char new_buf_for_write[8192];
	sprintf(new_buf_for_write,"%s",lines[i]);

	output_fd = open(file,O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if(output_fd == -1 ){
		printf("File open failed %s %s\n",strerror(errno),__FUNCTION__);
	}

	sprintf(buf,"HTTP/1.1 %d Created\r\n\r\n",success_code);
	write(output_fd,new_buf_for_write,content_len);

}
int parse_accept_encoding_header(char **lines,int maxlines,char *found) {
	char accept_encoding_singe_val[MAX_BYTES] = {0};
	char* accept_encoding_multiple_val[MAX_COMPRESSION_OPTIONS] ={0}; 
	int compression_enabled = 0;

	if(find_string_in_parsed_header("Accept-Encoding",lines,maxlines,accept_encoding_singe_val,accept_encoding_multiple_val) == 0) {
		if(accept_encoding_multiple_val[0] == NULL) {
			printf("Serach in single value of parsed headers\n");
			if(search_in_server_supportd_compresion(accept_encoding_singe_val) == 0 ) {
				memcpy(found,accept_encoding_singe_val,8192);				
				compression_enabled = 1;					
			}
		} else {
			printf("Serach in multiple values of parsed headers \n");
			for(int j = 0 ; j < MAX_COMPRESSION_OPTIONS && accept_encoding_multiple_val[j] != NULL ; j++) {
				char temp_to_compare[8192];	
				remove_crlf_from_end(accept_encoding_multiple_val[j],temp_to_compare);
				printf("String to search %s %s \n",temp_to_compare,accept_encoding_multiple_val[j]);
				if(search_in_server_supportd_compresion(temp_to_compare) == 0 ) {
					memcpy(found,temp_to_compare,8192);
					compression_enabled = 1;
					break;
				}
			}
		}
	}
	return compression_enabled;
}
void get_response(char *uri,char** lines,int maxlines,char *buf){
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
		printf("Echo request recieved \n");
		
		int prev_index = 0;
		success_code = 200;
		int bytes_written = sprintf(buf+prev_index,"HTTP/1.1 %d OK\r\n",success_code);
		prev_index += bytes_written;
		bytes_written = sprintf(buf+prev_index,"Content-Type: text/plain\r\n");

		char accept_encoding_val[8192] = {0};
		char dst[8192] = {0};
		int dst_len = 0;
		if(parse_accept_encoding_header(lines,maxlines,accept_encoding_val) == 1) {
			printf("Encoding/Compression is enabled\n");
			prev_index += bytes_written;
			bytes_written = sprintf(buf+prev_index,"Content-Encoding: %s\r\n",accept_encoding_val);

			gzip_compression(tokens[i-1],dst,&dst_len);

		} else {
			dst_len = strlen(tokens[i-1]);
			memcpy(dst,tokens[i-1],dst_len);

		}
		printf("Buffers and stats dst_buffer %s\n dst_len %d\n",dst,dst_len);
		prev_index += bytes_written;
		bytes_written = sprintf(buf+prev_index,"Content-Length: %d\r\n",dst_len);
		prev_index += bytes_written;
		bytes_written = sprintf(buf+prev_index,"\r\n");
		prev_index += bytes_written;
		memcpy(buf+prev_index,dst,dst_len);
		return;
	}

	if(i >= 1 && strcasecmp(tokens[i-1],"User-agent") == 0) {	
		printf("User-agent request\n");
		char found[MAX_BYTES] = {0};
		success_code = 200;
		int prev_index = 0;
		int bytes_written = 0;
		int len = 0;
		if(handle_user_agent_query(lines,maxlines,found) == 1 && found != NULL) {
			bytes_written = sprintf(buf,"HTTP/1.1 %d OK\r\nContent-Type: text/plain\r\n",success_code);
			prev_index += bytes_written;
			len = strlen(found);
			bytes_written = sprintf(buf+prev_index,"Content-Length: %d\r\n\r\n%s",len,found);
		}
		return;
	}

	if(i == 2 && strcmp(tokens[i-2],"files") == 0) {
		printf("File content get request\n");
		int success_code = 404;
		int prev_index = 0;
		int bytes_written = 0;
		int len = 0;

		char file_content[MAX_BYTES] = {0};
		if(handle_file_get_content_request(tokens[i-1],file_content,&len) == 1) {
			sprintf(buf,"HTTP/1.1 %d Not Found\r\n\r\n",success_code);
		} else {
			success_code = 200;
			bytes_written = sprintf(buf+prev_index,"HTTP/1.1 %d OK\r\nContent-Type: application/octet-stream\r\n",success_code);
			prev_index += bytes_written;
			bytes_written = sprintf(buf+prev_index,"Content-Length: %d\r\n\r\n",len);
			prev_index += bytes_written;
			memcpy(buf+prev_index,file_content,len);
			printf("Final content for file handling buf %s\n len %d\n",buf,len);
		}
		return;
	}

	//Now check the final file, no need to further parrse
	if(stat(uri,&st) == -1) {
		printf("File not found %s \n",strerror(errno));
		success_code = 404;
		sprintf(buf,"HTTP/1.1 %d Not Found\r\n\r\n",success_code);
	} else {
		printf("File found  \n");
		sprintf(buf,"HTTP/1.1 %d OK\r\n\r\n",success_code);
	}
	 
	
}

void post_response_for_endpoint(char* uri,char **lines,int maxlines,char *buf) {
	
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

	if(i >= 2 && strcmp(tokens[i-2],"files") == 0) {
		printf("Create file with the content from post request :->%s\n",__FUNCTION__);
		handle_file_post_request(tokens[i-1],lines,maxlines,buf);
		return;
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
	parse_req_into_lines(lines,&line,buf_recv);

	char method[MAX_BYTES],uri[MAX_BYTES];
	sscanf(lines[0],"%s %s",method,uri);
	printf("Method %s Uri %s \n",method,uri);

	char buf_send[MAX_BYTES] = {0};
	if(strcasecmp(method,"GET") == 0) {
		printf("Get rquest received\n");
		get_response(uri,lines,line,buf_send);

	} else if(strcasecmp(method,"POST") == 0) {
		printf("Post rquest received\n");
		post_response_for_endpoint(uri,lines,line,buf_send);

	} else {
		printf("Not supported yet\n");
	}

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
