#include "lsp.h"
#include "lspmessage.pb-c.h"
#include "list.h"
#include <stdint.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

//length
#define BUFFER_LENGTH 100
#define NETDB_MAX_HOST_NAME_LENGTH 256

#define FALSE 0
#define TRUE 1

//server constant
#define SERVER_NAME "127.0.0.1"
#define SERVER_PORT 3555
#define MAX_THREADS 20

//marshaling constant
#define MAX_MSG_SIZE 1024
#define MAX_PAYLOAD_SIZE 1000

unsigned int SLEEP_TIME = 2;
unsigned int TIME_OUT = 100000;
double DROP_RATE = 0.5;
struct threadData data;

double gen_random()
{
    double ran_num = (rand()/(double)RAND_MAX);
    //printf("Drop rate == [%f]\n",ran_num);
    return ran_num;
    
}

int compare_connection1 (const void* search, const void* data)
{
    connection* conn = (connection*)data;
    uint32_t key = * ((uint32_t*)search);
    
    if ( key == conn->connection_id)
    {
        return 1;
    }
    return 0;
}

int compare_connection2(const void* c1, const void* c2)
{
    if(((connection*)c1)->connection_id != ((connection*)c2)->connection_id )
        return 0; 
    else
        return 1;
}

int compare_address(const void* c1, const void* c2)
{
    struct sockaddr_in* fromaddr = (struct sockaddr_in*) c1;
    connection* conn = (connection*)c2;
    if((fromaddr->sin_addr.s_addr == conn->clientaddr.sin_addr.s_addr) && (fromaddr->sin_port == conn->clientaddr.sin_port))
        return 1;
    else return 0;
}

typedef struct connid_seq
{
    uint32_t connid;
    uint32_t seq;
}connid_seq;

int compare_msg(const void* c1, const void* c2)
{
    uint32_t connid = ((connid_seq*)c1)->connid;
    uint32_t seq = ((connid_seq*)c1)->seq;
    LSPMessage* msg = (LSPMessage*)c2;
    if (connid == msg->connid && seq == msg->seqnum)
    {
        return 1;
    } 
    else
    {
        return 0;
    }
}

void free_func(void* data)
{
    //
}

void free_msg(void* data)
{
    //lspmessage__free_unpacked ((LSPMessage*)data, NULL);
}


 bool is_connection_request(LSPMessage* message)
 {
    if((message->connid ==0 && message->seqnum == 0) && (memcmp(message->payload.data,"NIL",3) == 0))
        return true;
    else
        return false;
 }

 bool is_ack(LSPMessage* message)
 {
    if( ( message->connid != 0 ) && ( memcmp(message->payload.data,"NIL",3) == 0 ) )
    {
        return true;
    }
    else
        return false;
    
 }

 bool is_data(LSPMessage* message)
 {
    if( (message->connid != 0) && ( memcmp(message->payload.data,"NIL",3) != 0) )
        return true;
    else
        return false;
 }

void box_push(list* box, LSPMessage* msg)
{
    //printf("box push\n");
    pthread_mutex_lock(&box->lock);
    push_back(box, msg);
    pthread_mutex_unlock(&box->lock);
}

void box_delete(list* box, uint32_t connid, uint32_t seq)
{
    //printf("box delete\n");
    pthread_mutex_lock(&box->lock);
    connid_seq cs;
    cs.connid = connid;
    cs.seq = seq;
    remove_data(box, &cs, compare_msg, free_msg);
    pthread_mutex_unlock(&box->lock); 
}

void box_pop(list* box)
{
    //printf("box pop\n");
    pthread_mutex_lock(&box->lock);
    remove_front(box, free_msg);
    pthread_mutex_unlock(&box->lock);   
}

LSPMessage* box_peek(list* box)
{
    //printf("box peek\n");
    pthread_mutex_lock(&box->lock);
    LSPMessage* msg = front(box);
    pthread_mutex_unlock(&box->lock);
    return msg;
}

bool box_find(list* box,LSPMessage* msg)
{
    bool exist;
    connid_seq cs;
    cs.connid = msg->connid;
    cs.seq = msg->seqnum;
    pthread_mutex_lock(&box->lock);
    //printf("box find\n");
    if(find_occurrence(box, &cs, compare_msg))
        exist = true;
    else
        exist = false;
    pthread_mutex_unlock(&box->lock);
    return exist;
}


int marshal_send(uint32_t sock,struct sockaddr_in* sockaddr,LSPMessage* msg)
{

    double rand = 0;
//    if((rand = gen_random()) < DROP_RATE)
//    {
//        //printf("%f\n",rand);
//        printf("Send packet dropped.\n");
//        return 0;
//    }
     
    uint8_t* buf = malloc(MAX_MSG_SIZE);
    int rc = 0;
    uint8_t send_msg_len = lspmessage__get_packed_size(msg);
    lspmessage__pack(msg, buf);
    
//    printf("Marshal send\n");
//    for(int i = 0;i<send_msg_len;i++)
//        printf("%x ",buf[i]);
 //   printf("\n");
    
//    msg = lspmessage__unpack(NULL, send_msg_len, buf); 

//    printf("Restored msg data[%s] len[%d]\n",msg->payload.data,msg->payload.len);

        
    rc = sendto(sock, buf, send_msg_len, 0,  (struct sockaddr *)sockaddr,sizeof(struct sockaddr));
    if(rc == -1)
    {
        perror("server: sendto");
        return -1;
    }

    free(buf);
    return rc;
}

void* server_epoch_handler(void* data)
{
    lsp_server* server = (lsp_server*)data;
    int i;
    struct lnode* current;
    while(1)
    {
//        printf("epoch running\n");
        pthread_mutex_lock(&server->outbox_list->lock);
        current = server->outbox_list->head;
        for(i =0; i< server->outbox_list->size; i++)
        {
            LSPMessage* msg = (LSPMessage*)(current->data);
            
            //printf("---------conn mutex\n");
            pthread_mutex_lock(&server->connecion_list->lock);
            connection* conn = find_occurrence(server->connecion_list,&(msg->connid), compare_connection1);
            //printf("---------in conn mutex 241\n");
            pthread_mutex_unlock(&server->connecion_list->lock);
            
            if(conn)
            {
//                printf("Server epoch send data to id[%d]\n", conn->connection_id);
                marshal_send(server->listen_socket,&conn->clientaddr,current->data);
            }
            else 
            {
 //               printf("Connection lost\n");
            }
                
            current = current->next;
        } 
        pthread_mutex_unlock(&server->outbox_list->lock);
        
        pthread_mutex_lock(&server->connecion_list->lock);
        
        current = server->connecion_list->head;
        for(i =0; i< server->connecion_list->size; i++)
        {
            connection* conn = (connection*)current->data;
           
            LSPMessage* send_msg = malloc(sizeof(LSPMessage));
            lspmessage__init(send_msg);
            send_msg->connid = conn->connection_id;
                      
            pthread_mutex_lock(&conn->recv_squence_guard);
            send_msg->seqnum = conn->recv_sequence - 1;
            pthread_mutex_unlock(&conn->recv_squence_guard);
            
            send_msg->payload.data = malloc(sizeof(uint8_t)*strlen("NIL"));
            send_msg->payload.len = strlen("NIL");
            memcpy(send_msg->payload.data, "NIL", sizeof(uint8_t)*strlen("NIL"));
            
            if(conn)
            {
                //printf("server epoch Ack: connid [%s]seqnum [%d]\n",send_msg.connid,send_msg.seqnum); 
                marshal_send(server->listen_socket,&conn->clientaddr,send_msg);
            }
            
            if(!conn->packet_recv_in_this_epoch)
                conn->timeout++;
            else
            {
                conn->timeout = 0;
                conn->packet_recv_in_this_epoch = false;
            }
            if(conn->timeout > TIME_OUT)
            {              
                pthread_mutex_lock(&server->disconnect_event_queue->lock);
                sem_post(&server->disconnect_event_queue->sem);
                disconnect_event* e = (disconnect_event*)malloc(sizeof(disconnect_event));
                e->connid = conn->connection_id;
                push_back(server->disconnect_event_queue,e);
                pthread_mutex_unlock(&server->disconnect_event_queue->lock);
//                printf("connection [%d] is removed\n",conn->connection_id);
                remove_data(server->connecion_list, conn, compare_connection2, free_func); 
            }
               
            current = current->next;
        }
        //printf("---------in conn mutex 293\n");
        pthread_mutex_unlock(&server->connecion_list->lock);
        sleep(SLEEP_TIME);
    }
}

void* client_epoch_handler(void* data)
{
    lsp_client* client = (lsp_client*)data;
    int i;
    while(1)
    {        
        /*inbox send ack*/
        pthread_mutex_lock(&client->recv_squence_guard);
        if(client->recv_sequence > 1)
        {         
            LSPMessage* send_msg = malloc(sizeof(LSPMessage));
            lspmessage__init (send_msg);

            send_msg->connid = client->connection_id;
            send_msg->seqnum = client->recv_sequence - 1;
            send_msg->payload.data = malloc(sizeof(uint8_t)*strlen("NIL"));
            send_msg->payload.len = strlen("NIL");
            memcpy(send_msg->payload.data, "NIL", sizeof(uint8_t)*strlen("NIL"));
            //printf("Client epoch Ack: [%s][%d]\n",send_msg.payload.data,send_msg.payload.len);
            marshal_send(client->socket,&client->serveraddr, send_msg);
        }
        pthread_mutex_unlock(&client->recv_squence_guard);
        
        pthread_mutex_lock(&client->outbox_list->lock);
        node* current = client->outbox_list->head;
        for(i = 0; i< client->outbox_list->size; i++)
        {
            LSPMessage* msg = (LSPMessage*)(current->data);
 //           printf("Client data send: pld[%s]len[%d]connid[%d]seqnum[%d]\n",msg->payload.data,msg->payload.len,msg->connid,msg->seqnum);
            marshal_send(client->socket,&client->serveraddr, current->data);
            current = (node*)current->next;
        }    
        pthread_mutex_unlock(&client->outbox_list->lock);
            
        if(client->packet_recv_in_this_epoch == false)    
            client->timeout++;
        else
        {
            client->timeout = 0;
            client->packet_recv_in_this_epoch = false;
        }
        if(client->timeout > TIME_OUT)
        {   
            printf("Disconnected\n");
            pthread_mutex_lock(&client->termintate_flag_guard);
                client->is_server_down = true;
            pthread_mutex_unlock(&client->termintate_flag_guard);  
            pthread_exit(NULL);
        }
        sleep(SLEEP_TIME);
    }
}

void epoch_start(void* param,epoch_handler handler)
{
    pthread_t id;
// create and detach the timer
    pthread_create(&id, NULL, handler, param);
    pthread_detach(id);
}

void lsp_set_epoch_lth(double lth)
{
    SLEEP_TIME = lth;//data.sleepTime = lth;
}
void lsp_set_epoch_cnt(int cnt)
{
    TIME_OUT = cnt;//data.timeout_count = cnt;
}

void lsp_set_drop_rate(double rate)
{
    srand(time(0));
    DROP_RATE = rate;
}

void network_start(void* param, network_handler handler)
{
    pthread_t id;
    pthread_create(&id, NULL, handler, (void*)param);
    pthread_detach(id);
}


void* client_network_handler(void* client)
{
    lsp_client* a_client = (lsp_client*)client;
    
    struct sockaddr_in fromaddr;
    char* buf;
    buf = malloc(MAX_MSG_SIZE);
    uint32_t addrlen = sizeof(struct sockaddr_in);
    int rc;

    while(1)
    {    
        if((rc = recvfrom(a_client->socket, buf, MAX_MSG_SIZE, 0,(struct sockaddr*)&fromaddr, &addrlen)) == -1)
        {
            perror("Client recvfrom error.\n");
        }
        
        if(gen_random() < DROP_RATE)
        {
 //           printf("Recv packet dropped.\n");
            memset(buf,0,MAX_MSG_SIZE);       
            continue;
        }
        
//        printf("Client recv:\n");
//        for(int i = 0;i<rc;i++)
//            printf("%x ",buf[i]);
//        printf("\n");
        
        a_client->packet_recv_in_this_epoch = true;
        LSPMessage* message = lspmessage__unpack(NULL, rc, buf);
       
        if ( !message )
        {
            printf("Message format is wrong");
            continue;
        }
        
        if(is_ack(message))
        {
  //          printf("Client Ack recv: connid: [%d] seqnum: [%d] payload: [%s] len [%d]\n",message->connid,message->seqnum,message->payload.data,message->payload.len);
            if(a_client->connection_id == 0)//in wait connection state
            {
                a_client->recv_sequence++;
                a_client->connection_id = message->connid;      
                box_delete(a_client->outbox_list,0,message->seqnum);                   
            }
            else box_delete(a_client->outbox_list,message->connid,message->seqnum);
        }
        else if(is_data(message))
        {         
 //           printf("Client data recv: connid: [%d] seqnum: [%d] payload: [%s] len[%d]\n",message->connid,message->seqnum,message->payload.data, message->payload.len);          
            pthread_mutex_lock(&a_client->recv_squence_guard);
            if(message->seqnum != a_client->recv_sequence)
            {
//                printf("Wrong seq num, seq = [%d] expected seq = [%d]\n",message->seqnum,a_client->recv_sequence);
                memset(buf,0,MAX_MSG_SIZE);
                lspmessage__free_unpacked(message, NULL);
                pthread_mutex_unlock(&a_client->recv_squence_guard);
                continue;
            }
           
            a_client->recv_sequence++;
            pthread_mutex_unlock(&a_client->recv_squence_guard);
            
            LSPMessage* send_msg = malloc(sizeof(LSPMessage));
            lspmessage__init (send_msg);
            send_msg->connid = message->connid;
            send_msg->seqnum = message->seqnum;
            
  //          printf("client send ack connid = [%d] seq = [%d]\n",send_msg->connid,send_msg->seqnum);
            send_msg->payload.data = malloc(sizeof(uint8_t)*strlen("NIL"));
            send_msg->payload.len = strlen("NIL");
            memcpy(send_msg->payload.data, "NIL", strlen("NIL"));
    //        printf("Client Ack send: connid [%d] seqnum [%d]\n",send_msg->connid,send_msg->seqnum);
            marshal_send(a_client->socket,&a_client->serveraddr,send_msg);
            box_push(a_client->inbox_list,message);
        }
        else
        {
            lspmessage__free_unpacked(message, NULL);
        }
        memset(buf,0,MAX_MSG_SIZE);
        
        pthread_mutex_lock(&a_client->termintate_flag_guard);
        bool down_flag = a_client->is_server_down;
        pthread_mutex_unlock(&a_client->termintate_flag_guard);  
        if(down_flag)
        pthread_exit(NULL);
    }
}

lsp_client* lsp_client_create(const char* host, int port)
{
    int rc;
    
    // initialize a lsp_client and its send/recv buffer
    lsp_client* client = malloc(sizeof(lsp_client)); //take care of malloc
    client->connection_id = 0;
    client->send_sequence = 0;
    client->recv_sequence = 0;
    client->packet_recv_in_this_epoch = true;
    pthread_mutex_init(&(client->termintate_flag_guard),NULL);
    client->is_server_down = false;
    client->outbox_list = create_list();
    client->inbox_list = create_list();
    /////////////////////////////////

    // set up socket parameter
    if((client->socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("Client socket fail.n");
    }
    
    memset ( &client->serveraddr, 0, sizeof(client->serveraddr));
    client->serveraddr.sin_family = AF_INET;
    client->serveraddr.sin_port = htons(port);
    client->serveraddr.sin_addr.s_addr = inet_addr(host);

    if (client->serveraddr.sin_addr.s_addr == (unsigned long)INADDR_NONE)
    {
      // get the IP address from the dotted decimal
        struct hostent * host_addr = gethostbyname(host);
        if (host_addr == (struct hostent *) NULL)
        {
            perror("Host not found --> ");
            return NULL;
        }
        client->serveraddr.sin_addr.s_addr = *((unsigned long*)host_addr->h_addr_list[0]);             
    }

    
    network_start(client,client_network_handler);
    epoch_start(client,client_epoch_handler);
    
    // send a connection request
    lsp_client_write(client,"NIL",strlen("NIL"));
   
    return client;
}

bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth)
{
    LSPMessage* send_msg = malloc(sizeof(LSPMessage));
    lspmessage__init (send_msg);
    //---------------------------------SENDTO--------------------------------//
    //set up LSPMessage
    send_msg->connid = a_client->connection_id;
    
    pthread_mutex_lock(&a_client->send_squence_guard);
    send_msg->seqnum = a_client->send_sequence++;
    pthread_mutex_unlock(&a_client->send_squence_guard);
       
    send_msg->payload.data = malloc( sizeof(uint8_t)*lth);  
    send_msg->payload.len = lth;
    memcpy(send_msg->payload.data, (uint8_t*)pld, sizeof(uint8_t)*lth);
    //printf("Client write to outbox:[%s][%d]\n",send_msg.payload.data, send_msg.payload.len);
    
    box_push(a_client->outbox_list,send_msg);
    
    while(box_find(a_client->outbox_list,send_msg))
    {
        pthread_mutex_lock(&a_client->termintate_flag_guard);
        bool down_flag = a_client->is_server_down;
        pthread_mutex_unlock(&a_client->termintate_flag_guard);  
        if(down_flag)
            return false;
    }
        
    return true;
}

int lsp_client_read (lsp_client* a_client, uint8_t* pld )
{
    LSPMessage* recv_msg = NULL;
    while(!recv_msg)
    {
         recv_msg = box_peek(a_client->inbox_list);
         
         pthread_mutex_lock(&a_client->termintate_flag_guard);
         bool down_flag = a_client->is_server_down;
         pthread_mutex_unlock(&a_client->termintate_flag_guard);  
         if(down_flag)
             return -1;     
    }
    //printf("client read inbox: payload[%s] len[%d]",recv_msg->payload.data,recv_msg->payload.len);
    uint32_t length = recv_msg->payload.len;
    memcpy(pld,recv_msg->payload.data,recv_msg->payload.len);
    pld[recv_msg->payload.len] = '\0';
    lspmessage__free_unpacked(recv_msg, NULL);
    box_pop(a_client->inbox_list);
    return length;
}

void* server_network_recv_handler(void* server)
{
    lsp_server* s = (lsp_server*)server;
   
    struct sockaddr_in fromaddr;
    char* buf;
    buf = malloc(MAX_MSG_SIZE);
    uint32_t addrlen = sizeof(struct sockaddr_in);
    int rc;
    while(1)
    {      
//        printf("network running\n");
        rc = recvfrom(s->listen_socket, buf, MAX_MSG_SIZE, 0,(struct sockaddr*)&fromaddr, &addrlen);
//      printf("rc = [%d]\n", rc);
        if(rc == -1)
        {
            perror("Server recvfrom error.\n");
        }
        
        if(gen_random() < DROP_RATE)
        {
//            printf("Recv packet dropped.\n");
            memset(buf,0,MAX_MSG_SIZE);       
            continue;
        }
//      printf("Message...\n");
        LSPMessage* message = lspmessage__unpack(NULL, rc, buf);     
        if(is_connection_request(message))
        {                        
            printf("server connection recved\n");
            pthread_mutex_lock(&s->connecion_list->lock);
            connection* conn = find_occurrence(s->connecion_list, &fromaddr, compare_address);
//            printf("---------networkhandler in conn mutex 590\n");
            pthread_mutex_unlock(&s->connecion_list->lock);
  //          if(conn)
//                printf("find conn.\n");
            if(!conn)
            {       
//                printf("-----------------not conn\n");
                connection* new_connection = malloc(sizeof(connection));
                pthread_mutex_init(&(new_connection->recv_squence_guard),NULL);
                pthread_mutex_init(&(new_connection->send_squence_guard),NULL);
                new_connection->connection_id = ++s->connection_id_count;
                
//               printf("------------send_guard\n");
                pthread_mutex_lock(&new_connection->send_squence_guard);
                new_connection->send_sequence = 0;
                new_connection->send_sequence++;
                pthread_mutex_unlock(&new_connection->send_squence_guard);
                
//               printf("------------recv_guard\n");
                pthread_mutex_lock(&new_connection->recv_squence_guard);
                new_connection->recv_sequence = 0;
                new_connection->recv_sequence++;
                pthread_mutex_unlock(&new_connection->recv_squence_guard);
                
                memcpy(&new_connection->clientaddr,&fromaddr, sizeof(struct sockaddr_in));
                new_connection->timeout = 0;
                new_connection->packet_recv_in_this_epoch = true;
                
//               printf("------------conn_list_lock\n");
                pthread_mutex_lock(&s->connecion_list->lock);
                push_back(s->connecion_list, new_connection);
//                printf("---------networkhandler in conn mutex 621\n");
                pthread_mutex_unlock(&s->connecion_list->lock);

                LSPMessage* send_msg = malloc(sizeof(LSPMessage));
                lspmessage__init (send_msg);
                send_msg->connid = new_connection->connection_id;
                send_msg->seqnum = 0;
                send_msg->payload.data = malloc(sizeof(uint8_t)* strlen("NIL"));
                send_msg->payload.len = strlen("NIL");
                memcpy(send_msg->payload.data, "NIL", strlen("NIL"));
                printf("server connection request response: connid [%d] \n",send_msg->connid);
                marshal_send(s->listen_socket,&new_connection->clientaddr,send_msg);
            }
            lspmessage__free_unpacked(message, NULL);
        }
        else if(is_ack(message))
        {   
//            printf("server ack recved: connid [%d] seqnum [%d]\n",message->connid,message->seqnum);
            pthread_mutex_lock(&s->connecion_list->lock);
            connection* conn = find_occurrence(s->connecion_list, &message->connid, compare_connection1);
//            printf("---------networkhandler in conn mutex 640\n");
            pthread_mutex_unlock(&s->connecion_list->lock);
            if(!conn)
                continue;
            conn->packet_recv_in_this_epoch = true;
            box_delete(s->outbox_list,message->connid,message->seqnum);
            lspmessage__free_unpacked(message, NULL);
        }
        else if(is_data(message))
        {
//            printf("server data recved: connid [%d] seqnum [%d] payload [%s] len [%d]\n",message->connid,message->seqnum,message->payload.data,message->payload.len);
            pthread_mutex_lock(&s->connecion_list->lock);
            connection* conn = find_occurrence(s->connecion_list, &message->connid, compare_connection1);
//            printf("---------networkhandler in conn mutex 653\n");
            pthread_mutex_unlock(&s->connecion_list->lock);
            if(!conn)
            {       
//               printf("Data recv but cant find connection.\n");
                continue;
            }
              
            pthread_mutex_lock(&conn->recv_squence_guard);
            if(message->seqnum != conn->recv_sequence)
            {
//                printf("Seqnum wrong, expect  = [%d].\n",conn->recv_sequence,message->seqnum);
                lspmessage__free_unpacked(message, NULL);
                memset(buf,0,MAX_MSG_SIZE);  
                pthread_mutex_unlock(&conn->recv_squence_guard); 
                continue;
            }
            conn->recv_sequence++;
            pthread_mutex_unlock(&conn->recv_squence_guard);           
            conn->packet_recv_in_this_epoch = true;
                     
            LSPMessage* send_msg = malloc(sizeof(LSPMessage));
            lspmessage__init (send_msg);
            send_msg->connid = message->connid;
            send_msg->seqnum = message->seqnum;
            send_msg->payload.data = malloc(sizeof(uint8_t)*strlen("NIL"));
            send_msg->payload.len = strlen("NIL");
            memcpy(send_msg->payload.data, "NIL", strlen("NIL"));
//            printf("server send ack: connid [%d] seqnum [%d]\n",send_msg->connid,send_msg->seqnum);
            marshal_send(s->listen_socket,&conn->clientaddr,send_msg);
            box_push(s->inbox_list,message);
        }
        else
        {
 //           printf("nothing...\n");
            lspmessage__free_unpacked(message, NULL);
        }
        
        memset(buf,0,MAX_MSG_SIZE);       
    } 
}


bool lsp_client_close(lsp_client* a_client)
{
    if ( a_client->socket != -1)
        close ( a_client->socket );

    empty_list(a_client->inbox_list, free_msg);
    empty_list(a_client->outbox_list, free_msg);
    pthread_mutex_destroy(&a_client->termintate_flag_guard);
    free(a_client);
    
    return TRUE; //When will return FALSE;
}


lsp_server* lsp_server_create(int port)
{
    int rc;
    lsp_server* server = malloc(sizeof(lsp_server));
  
    server->listen_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->listen_socket < 0)
    {
        perror("Socket create fail.\n");
    }

    int optval = 1;
    rc = setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    if(rc == -1)
    {
        perror("Setsockpot error.\n");
    }
    
    memset(&server->serveraddr, 0, sizeof(server->serveraddr));
    server->serveraddr.sin_family = AF_INET;
    server->serveraddr.sin_port = htons((uint16_t)port);
    server->serveraddr.sin_addr.s_addr = INADDR_ANY;

    if((rc = bind(server->listen_socket, (struct sockaddr *)&server->serveraddr,
          sizeof(server->serveraddr))) == -1 )
    {
        perror("Bind to port fail.\n");
    }

    server->connection_id_count = 0;
    server->connecion_list = create_list();
    server->disconnect_event_queue = create_list();
    server->inbox_list = create_list(); 
    server->outbox_list = create_list();
    
  
    network_start(server,server_network_recv_handler);
    epoch_start(server,server_epoch_handler);
    
    return server;
    //When will return NULL
}

bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id)
{
    LSPMessage* send_msg = malloc(sizeof(LSPMessage));
    lspmessage__init (send_msg);
    
    connection* conn;
    pthread_mutex_lock(&a_srv->connecion_list->lock);
    conn = find_occurrence(a_srv->connecion_list, &conn_id, compare_connection1);
    if(!conn)
    {
        pthread_mutex_unlock(&a_srv->connecion_list->lock);
        return false;
    }
        
    //printf("---------serverwrite in conn mutex 757\n");
    pthread_mutex_unlock(&a_srv->connecion_list->lock);
    
    send_msg->connid = conn_id;
    
    pthread_mutex_lock(&conn->send_squence_guard);
    send_msg->seqnum = conn->send_sequence++;
    pthread_mutex_unlock(&conn->send_squence_guard);
      
    send_msg->payload.data = malloc( sizeof(uint8_t)*lth );  //Is this correct?
    send_msg->payload.len = lth;
    memcpy (send_msg->payload.data, (uint8_t*)pld, sizeof(uint8_t)*lth);
    
    uint8_t* buf = malloc(MAX_MSG_SIZE);
    int msg_len = lspmessage__pack (send_msg, buf);
    LSPMessage* p_send_msg = lspmessage__unpack(NULL, msg_len, buf);   
    
//    printf("Server write to outbox:data[%s] len [%d]connid[%d] seq[%d]\n",send_msg->payload.data, send_msg->payload.len,send_msg->connid,send_msg->seqnum);
    box_push(a_srv->outbox_list,send_msg);
    
    while(box_find(a_srv->outbox_list,p_send_msg))
    {
        pthread_mutex_lock(&a_srv->connecion_list->lock);
        if(!find_occurrence(a_srv->connecion_list,&conn_id, compare_connection1))
        {
            pthread_mutex_unlock(&a_srv->connecion_list->lock);  
            return false;
        }   
        pthread_mutex_unlock(&a_srv->connecion_list->lock);
    }
    return true;/*?*/
}

int lsp_server_read(lsp_server* a_srv, uint8_t* pld, uint32_t* conn_id)
{
    uint32_t payload_len = 0;
    LSPMessage* msg = NULL;
    while(msg == NULL)
    {
        msg = box_peek(a_srv->inbox_list);
    }

 //   printf("read from box connid [%d] seq [%d] pld [%s] len [%d]\n",msg->connid,msg->seqnum,msg->payload.data,msg->payload.len);
        
    *conn_id = msg->connid;
    payload_len = msg->payload.len;
    memcpy(pld,msg->payload.data,payload_len);
    pld[payload_len] = '\0';
    box_pop(a_srv->inbox_list);
    return payload_len;
}

bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id)
{
    pthread_mutex_lock(&a_srv->connecion_list->lock);
    connection* conn = find_occurrence(a_srv->connecion_list, &conn_id, compare_connection1);
    pthread_mutex_unlock(&a_srv->connecion_list->lock);
    if(conn)
    {
        remove_data(a_srv->connecion_list, conn, compare_connection2, free_func);
    }
    
    return true;
}
