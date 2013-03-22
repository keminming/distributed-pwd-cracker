#pragma once

#include "lspmessage.pb-c.h"
#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <strings.h>
#include <errno.h>
#include <string.h>

// Global Parameters. For both server and clients.

#define _EPOCH_LTH 2.0
#define _EPOCH_CNT 5;
#define _DROP_RATE 0.0;

//---------------------
#define STAT_PAUSE 0
#define STAT_SEND 1
#define STAT_RECV 2


#define NOACK 0
#define ACK 1

#define ERROR_SUCCESS 0
#define ERROR_FAIL -1
//--------------------------------

////////////////////////////////
#define MAX_MSG_SIZE 1024
////////////////////////////////
int length(char* string);
////////////////////////////////

//-------------------------
struct threadData {
    int sleepTime;
    int timeout_count;
    void* param;
};
//------------------------------------

typedef void* (*network_handler)(void*);
typedef void* (*epoch_handler)(void*);
//-----------------------------------
void resend(struct threadData * td);
void *timer (void *data);
//------------------------------------
void lsp_set_drop_rate(double rate);
void lsp_set_epoch_lth(double lth);
void lsp_set_epoch_cnt(int cnt);


////////////////////////////////
typedef struct connection
{
    uint32_t connection_id;
    uint32_t send_sequence;
    pthread_mutex_t send_squence_guard;
    uint32_t recv_sequence;
    pthread_mutex_t recv_squence_guard;
    struct sockaddr_in clientaddr;
    uint8_t timeout;
    bool packet_recv_in_this_epoch;
}connection;

typedef struct 
{
    uint32_t socket;
    uint32_t connection_id;
    uint32_t send_sequence;
    pthread_mutex_t send_squence_guard;
    uint32_t recv_sequence;
    pthread_mutex_t recv_squence_guard;
    
    struct sockaddr_in serveraddr;
    uint8_t timeout;
    pthread_mutex_t termintate_flag_guard; 
    bool is_server_down;
    bool packet_recv_in_this_epoch;
    
    list* outbox_list;
    list* inbox_list;
}lsp_client;
/////////////////////////////////////

typedef struct disconnect_event
{
    uint32_t connid;
}disconnect_event;


lsp_client* lsp_client_create(const char* host, int port);
int lsp_client_read(lsp_client* a_client, uint8_t* pld);
bool lsp_client_write(lsp_client* a_client, uint8_t* pld, int lth);
bool lsp_client_close(lsp_client* a_client);

/////////////////////////////////


typedef struct lsp_server
{
    int listen_socket;
    struct sockaddr_in serveraddr;

    int connection_id_count;
    list* connecion_list;
    list* disconnect_event_queue;

    list* outbox_list;
    list* inbox_list;
}lsp_server;

lsp_server* lsp_server_create(int port);
int  lsp_server_read(lsp_server* a_srv, uint8_t* pld, uint32_t* conn_id);
bool lsp_server_write(lsp_server* a_srv, void* pld, int lth, uint32_t conn_id);
bool lsp_server_close(lsp_server* a_srv, uint32_t conn_id);
