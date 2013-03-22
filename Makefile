CC = gcc -std=c99

LDFLAGS = -g 
CFLAGS = -g 
LIB = -L/usr/lib -lpthread
INCLUDE = -I/usr/include

all: server request worker
	
server: lsp.o list.o lspmessage.pb-c.o sample_server.o
	$(CC) $(CFLAGS) $^ -o $@ -I/usr/include -L/usr/lib -lpthread -lprotobuf-c `pkg-config --cflags --libs glib-2.0`
	
request: lsp.o list.o sha1.o lspmessage.pb-c.o sample_client.o
	$(CC) $(CFLAGS) $^ -o $@ -I/usr/include -L/usr/lib -lpthread -lprotobuf-c

worker: lsp.o list.o lspmessage.pb-c.o sha1.o sample_worker.o
	$(CC) $(CFLAGS) $^ -o $@ -I/usr/include -L/usr/lib -lpthread -lprotobuf-c

%.o:	%.c
	$(CC) -c $(CFLAGS) $< -o $@ -I/usr/include -L/usr/lib -lprotobuf-c `pkg-config --cflags --libs glib-2.0`


clean:
	rm -f *.o
	rm -f $(TARGET)



#gcc -std=c99 -g  hashtest.c -o hashtest `pkg-config --cflags --libs glib-2.0`

