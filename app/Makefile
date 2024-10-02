
all : server

%.o : %.c
	gcc -g -c $<

server : server.o
	gcc -g -o $@ $^
	
clean:
	rm -rf server server.o

