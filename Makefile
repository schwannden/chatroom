all: client server

client: client.o ../nplib/np_lib.o ../nplib/error_functions.o
	gcc -o client client.o  ../nplib/np_lib.o ../nplib/error_functions.o

client.o: client.c
	gcc -c client.c

server: server.o ../nplib/np_lib.o ../nplib/error_functions.o
	gcc -o server server.o  ../nplib/np_lib.o ../nplib/error_functions.o

server.o: server.c
	gcc -c server.c

clean:
	rm *.log *.o
