#include "lsp.h"
#include "lspmessage.pb-c.h"
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
#include <glib-2.0/glib.h>
#include "list.h"

/*
 * client parameter
 */

/*
 * marshaling parameter
 */

#define SERVER_PORT     5401
#define BUFFER_LENGTH    128
#define NETDB_MAX_HOST_NAME_LENGTH 256
#define HASH_PWD_SIZE 128
pthread_mutex_t hashmap_guard;
pthread_mutex_t job_hash_guard;
typedef struct Job
{
    char* job_msg;
    uint32_t msg_len;
    char start;
    char* hash;
}Job;

typedef struct Worker
{
    uint32_t conn_id;
    list* job_queue;
}Worker;

typedef struct Hash_element
{
    uint32_t connid;
    uint8_t* pwd;
    bool isFinished;
    int result_record[26];   
}Hash_element;

void free_worker(void* data)
{
    //
}

typedef struct param
{
    lsp_server* server;
    GHashTable* pwd_hash;
    GHashTable* job_hash;
    list* worker_queue;
    uint8_t* pld;
    uint32_t conn_id;
}param;

//"c3\n3\n12ABF551138756ADC2A88EDC23CB77B1832B7AB8'\0'"

//"fa\n3\n12ABF551138756ADC2A88EDC23CB77B1832B7AB8\naac"

//request: "ca\n3\nHASH"
//worker: "fa\n3\HASH\nPWD" "xa\n3\HASH"

int get_pld1(uint8_t* pld, uint8_t* hashed_pwd)
{
    uint8_t* pos1 = strstr(pld,"\n");    
    uint8_t* pos2 = strstr(pos1 + 1,"\n");
    uint8_t* len = malloc(8);

    memcpy(len, pos1+1, pos2-pos1-1);
    len[pos2-pos1-1]='\0';
    int pwd_len = atoi(len);
    
    strcpy(hashed_pwd, pos2+1);
    
    return pwd_len;
}



int get_pld2 ( uint8_t* pld, uint8_t* hashed_pwd, uint8_t* pwd )
{      
    uint8_t* pos1 = strstr(pld,"\n");    
    
    uint8_t* pos2 = strstr(pos1 + 1,"\n");
    uint8_t* len;
    len= malloc(8);
    memcpy(len, pos1+1, pos2-pos1-1);
    len[pos2-pos1-1]='\0';
    int pwd_len = atoi(len);
    
    uint8_t* pos3 = strstr(pos2+1, "\n");
    memcpy(hashed_pwd, pos2+1, pos3-pos2-1);
    hashed_pwd[pos3-pos2-1] = '\0';
    
    memcpy(pwd, pos3+1, pwd_len);
    
    return pwd_len;
}

Worker* worker_queue_front(list* worker_queue)
{
    pthread_mutex_lock(&worker_queue->lock);
    Worker* worker = (Worker*)front(worker_queue);
    if(!worker)
    {
        printf("Error: no worker\n");
        pthread_mutex_unlock(&worker_queue->lock);
        return NULL;
    }
//    printf("get worker [%d]. \n", worker->conn_id);
    pthread_mutex_unlock(&worker_queue->lock);
    return worker;
}


void join_handler(list* worker_queue, uint32_t conn_id)
{

//    printf("join_handler\n");
    Worker* worker = malloc(sizeof(Worker));
    worker->conn_id = conn_id;
    worker->job_queue = create_list();
   
    pthread_mutex_lock(&worker_queue->lock);
    printf("worker [%d] in queue. \n", worker->conn_id);
    push_back(worker_queue, worker);
    pthread_mutex_unlock(&worker_queue->lock);
}

//request: "ca\n3\nHASH"
void crack_handler(lsp_server* server,GHashTable* pwd_hash,GHashTable* job_hash,list* worker_queue,uint8_t* pld, uint32_t conn_id)
{
    printf("request from [%d] is received \ncracking...\n", conn_id);
    
    if ( worker_queue->size < 1 )
    {
        char* reply =  "n";
        lsp_server_write(server,reply, strlen(reply), conn_id);
        return;
    }
       
    
    uint8_t* buf = malloc(HASH_PWD_SIZE);
    
    //char* pwd = malloc(32);
    uint8_t* hashed_pwd = malloc(HASH_PWD_SIZE);
    
    int pwd_len = get_pld1(pld, hashed_pwd);
    if ( pwd_len == 0 )
    {
        perror("pswd format is wrong!\n");
        return;
    }

    Hash_element* element = malloc(sizeof(Hash_element));
    element->connid = conn_id;
    element->isFinished = false;
    element->pwd = malloc(32);

    for(int i = 0;i<26;i++)
    {
        element->result_record[i] = 0;//not known
    }
    
    Hash_element* is_e_in_pwd_hash;
    pthread_mutex_lock(&hashmap_guard);
    is_e_in_pwd_hash = (Hash_element*)g_hash_table_lookup(pwd_hash,hashed_pwd);
    if( is_e_in_pwd_hash)
    {
//        printf("Find this request [%s] in pwd_hash!\n", hashed_pwd);
        is_e_in_pwd_hash->connid = conn_id; 
        element->isFinished = is_e_in_pwd_hash->isFinished;
//        printf("????task si finished, [%d]", element->isFinished );
        for(int i=0; i< 26; i++)
        {
            element->result_record[i]= is_e_in_pwd_hash->result_record[i];
        }
        memcpy(element->pwd, is_e_in_pwd_hash->pwd, strlen(is_e_in_pwd_hash->pwd));
    }
    else
    {
        g_hash_table_insert(pwd_hash,hashed_pwd,element);
    }
    pthread_mutex_unlock(&hashmap_guard);
//    printf("hash map size [%d]", g_hash_table_size (pwd_hash));
            
    if ( element->isFinished == true )
    {
//        printf("request id [%d] is finished\n", element->connid);
        if (element->pwd && strlen(element->pwd) > 0 )
        {
//            printf("element pwd: [%s]\n", element->pwd);
            memset(buf, 0, HASH_PWD_SIZE);
            sprintf(buf,"f%s\n%s",pld,element->pwd);
            lsp_server_write(server,buf,strlen(buf),element->connid);
        }
        else
        {
            sprintf(buf,"x%s",pld);
            lsp_server_write(server,buf,strlen(buf),element->connid);
        }
        return;
    }
    

    for(int i=0; i<26; i++)
    {    
        if(element->result_record[i] != 0)
        {
            continue;
        }
        Job* job = malloc(sizeof(Job));
        char* key = malloc(HASH_PWD_SIZE);
        uint32_t worker_id;
        uint8_t start = 'a' + i;
        
        memset(buf, 0, HASH_PWD_SIZE);
        sprintf(buf,"%c\n%s",start,pld+2);

        Worker* worker = (Worker*)worker_queue_front(worker_queue);
        if(!worker)
        {
            break;
        }

        worker_id = worker->conn_id;
        job->job_msg = malloc(HASH_PWD_SIZE);
        strcpy(job->job_msg,buf);
        job->msg_len = strlen(buf);
        job->start = start;
        job->hash = malloc(HASH_PWD_SIZE);
        strcpy(job->hash,hashed_pwd);
        sprintf(key,"%c%s",job->start,job->hash);
//        printf("key in c_handle = [%s]\n",key);     

        pthread_mutex_lock(&job_hash_guard);
        g_hash_table_insert(job_hash,key,job);
        pthread_mutex_unlock(&job_hash_guard);

//        printf("////////////job_hash size [%d]\n", g_hash_table_size(job_hash));
        pthread_mutex_lock(&worker->job_queue->lock);
        push_back(worker->job_queue,job);
        pthread_mutex_unlock(&worker->job_queue->lock);

        pthread_mutex_lock(&worker_queue->lock);
        Worker* new_worker = malloc(sizeof(Worker));
        memcpy(new_worker,worker,sizeof(Worker)); 
        push_back(worker_queue, new_worker); 
        remove_front(worker_queue, free_worker);
        pthread_mutex_unlock(&worker_queue->lock);

 //       printf("write to client [%s]\n", buf); 
        lsp_server_write(server,buf,strlen(buf),worker_id);   
    }
    
}

int compare_job(const void* j1, const void* j2)
{
    Job* job1 = (Job*)j1;
    Job* job2 = (Job*)j2;
    if(job1 == job2)
        return true;
    else return false;
}

int compare_worker(const void* connid, const void* w2)
{
    uint32_t* pconnid = (uint32_t*)connid;
    Worker* worker2 = (Worker*)w2;
//    printf("*pconnid [%d] worker2->conn_id [%d]",*pconnid,worker2->conn_id);
    if(*pconnid == worker2->conn_id)
        return true;
    else return false;
}

void Find_handler(lsp_server* server,GHashTable* pwd_hash,GHashTable* job_hash, list* worker_queue,uint8_t* pld, uint32_t conn_id)
{   
//    printf("Find_handler\n");
    //worker: "fa\n3\HASH\nPWD" 
    uint8_t start = pld[0];
    uint8_t* hashed_pwd = malloc(HASH_PWD_SIZE);
    uint8_t* pwd = malloc(16);
    
    int pwd_len = get_pld2 ( pld, hashed_pwd, pwd );
    if(pwd_len == 0)
    {
        printf("pswd format is wrong\n");
        return;
    }
          
    char* key = malloc(128);
    sprintf(key,"%c%s",start,hashed_pwd);
//    printf("key in f_handle = [%s]\n",key);
//    printf("////////////job_hash size [%d]\n", g_hash_table_size(job_hash));
    pthread_mutex_lock(&job_hash_guard);
    Job* j = (Job*)g_hash_table_lookup(job_hash,key);
    pthread_mutex_unlock(&job_hash_guard);
    
    if(!j)
    {
//        printf("Error: can't find job.\n");
        return;
    }
    
    
    pthread_mutex_lock(&worker_queue->lock);
    Worker* worker = find_occurrence(worker_queue, &conn_id, compare_worker);
    pthread_mutex_unlock(&worker_queue->lock);
    
    if(!worker)
    {
        printf("Error: can't find worker.\n");
        return;
    }
               
    pthread_mutex_lock(&worker->job_queue->lock);
    remove_data(worker->job_queue,j,compare_job,free_worker);
    pthread_mutex_unlock(&worker->job_queue->lock);
    
    pthread_mutex_lock(&hashmap_guard);
    Hash_element* element = (Hash_element*)g_hash_table_lookup(pwd_hash,hashed_pwd);
    pthread_mutex_unlock(&hashmap_guard);
    
    if(!element)
    {         
//        printf("cant find element\n");
        return;
    }
    
    //element->connid: request; connid: worker
    element->result_record[start - 'a'] = 1;
    element->pwd = malloc(pwd_len); 
    memcpy(element->pwd,pwd,pwd_len); 
 //   printf("task finished\n");
    element->isFinished = true;
    
    uint8_t* msg = malloc(128);
    element->pwd[pwd_len]='\0';
    printf("The password for this request [%s] is [%s]\n", hashed_pwd, element->pwd);
    sprintf(msg,"f%s", pld);
    lsp_server_write(server,msg,strlen(msg),element->connid); 
}


void X_handler(lsp_server* server, GHashTable* pwd_hash,GHashTable* job_hash ,list* worker_queue ,uint8_t* pld, uint32_t conn_id)
{
 //   printf("X_handler\n");
    uint8_t* hashed_pwd = malloc(HASH_PWD_SIZE);
    uint8_t start = *pld;
    
    int pwd_len = get_pld1 ( pld, hashed_pwd ); 
    if(pwd_len == 0)
    {
        printf("pswd format is wrong\n");
        return;
    }

    char* key = malloc(128);
    sprintf(key,"%c%s",start,hashed_pwd);
    
 //   printf("key in x_handle = [%s]\n",key);
 //   printf("////////////job_hash size [%d]\n", g_hash_table_size(job_hash));
    pthread_mutex_lock(&job_hash_guard);
    Job* j = (Job*)g_hash_table_lookup(job_hash, key);
    pthread_mutex_unlock(&job_hash_guard);
    
    if(!j)
    {
  //      printf("Error: can't find job.\n");
        return;
    }
    
    pthread_mutex_lock(&worker_queue->lock);
    Worker* worker = find_occurrence(worker_queue, &conn_id, compare_worker);
    pthread_mutex_unlock(&worker_queue->lock);
    
    if(!worker)
    {
 //       printf("Error: can't find worker.\n");
        return;
    }
               
    pthread_mutex_lock(&worker->job_queue->lock);
    remove_data(worker->job_queue,j,compare_job,free_worker);
    pthread_mutex_unlock(&worker->job_queue->lock);
        
    pthread_mutex_lock(&hashmap_guard);
    Hash_element* element = (Hash_element*)g_hash_table_lookup(pwd_hash,hashed_pwd);
    pthread_mutex_unlock(&hashmap_guard);
    
    uint8_t* msg = malloc(128);
    
    if(!element)
    {         
//        printf("cant find element\n");
        return;
    }
    
    if (element->isFinished == true)
    {
        printf("The password for this request [%s] is [%s]", hashed_pwd, element->pwd);
        return;
    }
 
    element->result_record[start - 'a'] = -1; //false
    
    
    int i;
    for(i=0; i< 26; i++)
    {
        if(element->result_record[i] == 0)
        {
 //         printf("result[%d] has not been recorded\n", i);
          return;
        }   
    }
    element->isFinished = true;
    
    //"xa\n3\HASH"
    sprintf(msg,"x%s", pld);
 //   printf("-CCCCCCCCCCCCCC\n");
    lsp_server_write(server,msg,strlen(msg),element->connid); 
}

typedef int (*equal_op)(const void*, const void*);


void* worker_connection_monitor(void* parameter)
{
   
    param* p = (param*)parameter;
    while(1)
    {
 //       printf("worker_connection_monitor running.\n");
        sem_wait(&p->server->disconnect_event_queue->sem);
        pthread_mutex_lock(&p->server->disconnect_event_queue->lock);
        disconnect_event* e = front(p->server->disconnect_event_queue);
        remove_front (p->server->disconnect_event_queue, free_worker);
        pthread_mutex_unlock(&p->server->disconnect_event_queue->lock);  
        
        if(!e)
        {
 //           printf("Erorr: can't find dis event.\n");
            return NULL;
        }
        
        pthread_mutex_lock(&p->worker_queue->lock);
        Worker* worker = find_occurrence(p->worker_queue,&e->connid, compare_worker);
        if(!worker)
        {   
 //           printf("Error: can't find worker.\n");
            pthread_mutex_unlock(&p->worker_queue->lock);
            continue;
        }

        struct lnode* current = worker->job_queue->head;
        for(int i =0; i< worker->job_queue->size; i++)
        {
            Job* j = (Job*)(current->data);
            Worker* to_worker = front(p->worker_queue);
            if(to_worker == NULL)
            {
                printf("No worker available.\n");
                break;
            }
            if(to_worker == worker)
            {
                if(p->worker_queue->size == 1)
                {
                    printf("No worker available.\n");
                    break;
                }
                Worker* new_worker = malloc(sizeof(Worker));
                memcpy(new_worker,to_worker,sizeof(Worker));
                push_back(p->worker_queue,new_worker);
                remove_front(p->worker_queue,free_worker);
            }

            lsp_server_write(p->server,j->job_msg,j->msg_len,to_worker->conn_id); 

            current = current->next;
        } 
        
        remove_data(p->worker_queue, &e->connid, compare_worker, free_worker);
        
  //      printf("free worker connid [%d]",e->connid);
        pthread_mutex_unlock(&p->worker_queue->lock);
    }     
}



void* packet_handler(void* parameter)
{
    param* p = (param*)parameter;
    char type = p->pld[0];
    char* pld = malloc(128);
    strcpy(pld,p->pld + 1);
    switch(type)
    {
        case 'j':
            join_handler(p->worker_queue, p->conn_id);
            break;
        case 'c':
            crack_handler(p->server,p->pwd_hash,p->job_hash,p->worker_queue,pld, p->conn_id);
            break;
        case 'f':
            Find_handler(p->server,p->pwd_hash,p->job_hash,p->worker_queue,pld, p->conn_id);
            break;
        case 'x':
            X_handler(p->server, p->pwd_hash,p->job_hash,p->worker_queue,pld, p->conn_id);
            break;
        default:
            printf("Format error.\n");
            break;
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) 
{
    int port;
    if(argc > 1)
        port = atoi(argv[1]);
    else 
        port = SERVER_PORT;
    srand(12345);
    lsp_set_epoch_lth(2);
    lsp_set_epoch_cnt(5);
    lsp_set_drop_rate(0.01);
    list* worker_queue = create_list();
  
    GHashTable* pwd_hash = g_hash_table_new(g_str_hash , g_str_equal);

    pthread_mutex_init(&hashmap_guard,NULL);
    
    GHashTable* job_hash = g_hash_table_new(g_str_hash , g_str_equal);

    pthread_mutex_init(&job_hash_guard,NULL);

   
    uint8_t* pld;
    uint32_t conn_id;
    
    lsp_server* server = lsp_server_create(port);
    
    param* p  = (param*) malloc(sizeof(param));
    p->server = server;
    p->worker_queue = worker_queue;
    pthread_t id;
    pthread_create(&id,NULL,worker_connection_monitor,p);
    pthread_detach(id);

    for(;;)
    {  
   
        pld = malloc(MAX_MSG_SIZE);
        lsp_server_read(server, pld, &conn_id);
        if ( strlen(pld) > HASH_PWD_SIZE )
        {
            printf("Error: Message Exceed Max Buffer Size!");
            continue;
        }
        param* p  = (param*) malloc(sizeof(param));
        p->conn_id = conn_id;
        p->pld = pld;
        p->pwd_hash = pwd_hash;
        p->job_hash = job_hash;
        p->server = server;
        p->worker_queue = worker_queue;
        pthread_t id;
        pthread_create(&id,NULL,packet_handler,p);
	//printf("[%d] threads are created\n", id); 
        pthread_detach(id);
	   
        
    }
    
    sleep(1000000);
    return 0;
}

typedef void* (*server_packet_handler) (void* param);

