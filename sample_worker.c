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
#include "list.h"
#include "sha1.h"

/*
 * client parameter
 */

/*
 * marshaling parameter
 */

#define SERVER_NAME     "127.0.0.1"
#define SERVER_PORT     5401
#define BUFFER_LENGTH    100
#define FALSE              0
#define NETDB_MAX_HOST_NAME_LENGTH 256
#define HASH_PWD_SIZE 128

typedef struct 
{
    //uint32_t id;
    char start[1];
    uint32_t pwd_len;
    uint8_t* to_compare;
    uint8_t* pwd_result;
    bool is_found;
    
}pwd_crack;

typedef struct 
{
    lsp_client* worker;
    list* task_in_queue;
    //list* task_out_queue;
    
}param;

//request: "ca\n3\nHASH"
//worker: "fa\n3\nHASH\nPWD" "xa\n3\HASH"

//[b
//4
//53BE5352373E49D6E16FE976607336DB2B36D63C]

void parse_pld(uint8_t* pld, pwd_crack* task)
{
    uint8_t* pos1 = strstr(pld,"\n");
    memcpy(task->start, pld, pos1 - pld);
    
    uint8_t* pos2 = strstr(pos1 + 1,"\n");
    uint8_t* len;
    if ( (len= malloc(8)) == NULL )
    {
        printf("out of memory\n");
        exit(-1);
    }
    memcpy(len, pos1+1, pos2-pos1-1);
    len[pos2-pos1-1]='\0';
    task->pwd_len = atoi(len);
    
    task->to_compare = malloc(HASH_PWD_SIZE); 
    strcpy(task->to_compare, pos2+1);
    //printf("parse_pld, to_compare, [%s]\n", task->to_compare);
    //printf("post2+1: [%s]\n", pos2+1);
    free(len);
}


//request: "ca\n3\nHASH"
//worker: "fa\n3\nHASH\nPWD" "xa\n3\HASH"
void serialize_pld ( pwd_crack* task, uint8_t* pld)
{      
 //   printf("task, start[%c], len[%d], toCompare[%s], pwd[%s]\n", *(task->start), task->pwd_len, task->to_compare, task->pwd_result);
    char sign = 'x';
    if ( task->is_found == true )
    {
        sign = 'f';
        //printf("pwd_result [%s]\n", task->pwd_result);
        sprintf(pld,"%c%c\n%d\n%s\n%s", sign, *(task->start), task->pwd_len, task->to_compare, task->pwd_result);
    }
    else
    {
        //printf("pwd_len, %d\n", task->pwd_len);
        sprintf(pld,"%c%c\n%d\n%s", sign, *(task->start), task->pwd_len, task->to_compare);
    }
    //printf("pld in serialize [%s]\n", pld);
   
}

bool is_task_correct(pwd_crack* task)
{
    //printf("is_taks_correct\n");
    if ( task->pwd_len <= 0 || task->pwd_len >= 10 )
    {
        //printf("pwd_size wrong: [%d]\n", task->pwd_len); 
        return false;
    }
    else if ( *(task->start) < 'a' || *(task->start) > 'z' )
    {
        //printf("task start wrong [%c]\n", *(task->start) ); 
        return false;
    }
    else if ( task->to_compare == NULL )
    {
        //printf("error: Hash result (SHA1) \n");
        return false;
    }
    else 
        return true;
}

void free_worker_pwd(void* data)
{
    pwd_crack* task = (pwd_crack*)data;
    
    //free(task->pwd_result);
    free(task->to_compare);
}


void enqueue(list* queue, void* task)
{
    pthread_mutex_lock(&queue->lock);
    sem_post(&queue->sem);
    //printf("sem: [%d]\n", (queue->sem));
    push_back(queue, task);
    //printf("task_queue: size[%d]\n", queue->size);
    pthread_mutex_unlock(&queue->lock);
}

bool check(char* str, char* toCompare, size_t size)
{
    SHA1Context sha;
    int i;
    uint8_t hash_str[HASH_PWD_SIZE];
    
    memset(hash_str, 0, HASH_PWD_SIZE);
    bool return_value = false;

    SHA1Reset(&sha);
    SHA1Input(&sha, (const unsigned char *) str, size);
    
    //printf("check str: [%s]\n", str);
    if (!SHA1Result(&sha))
    {
        fprintf(stderr, "ERROR-- could not compute message digest\n");
    }
    else
    {
        char* s = malloc(8);
        
        int position = 0;
        //printf("SHA1:\t");
        for(i = 0; i < 5 ; i++)
        {
            //printf("%X ", sha.Message_Digest[i]);
            memset(s, 0, 8);
            sprintf(hash_str+position, "%X", sha.Message_Digest[i]);
            sprintf(s, "%X", sha.Message_Digest[i]);
            position += strlen(s);
        }
        free(s);
    }
    
    //printf("hash_str: <%s> \n", hash_str);

    if ( strcmp(hash_str, toCompare) == 0 )
    {
        //printf("check, true, <%s>\n", toCompare);
        return_value = true;
    }
    
    //printf("check, false, <%s>\n", toCompare);
    
    return return_value;
}


bool crack(char* offset, uint8_t pos, uint8_t size, uint8_t* toCompare, uint8_t* pswd_result)
{
    uint8_t i;
    if ( size == 1 )
    {
        for (i=0; i< 26; i++)
        {
            pswd_result[0] = 'a'+i;
            if ( check(pswd_result, toCompare, size) )
            {
                return true;
            }
        }
        return false;        
    }    
    else
    {
        for(i=0; i < 26; ++i)
        {
            pswd_result[pos+1] = 'a' + i;
            if ( pos + 1 < size -1 )
            {
                if(crack(offset, pos + 1, size, toCompare, pswd_result))
                {
                    //printf("Here, crack(pswd, pos + 1, size, toCompare)\n");
                    return true;
                }
            }
            else
            {
                pswd_result[0] = *offset;

                //printf("Check <%s> \n", pswd_result);
                if(check(pswd_result, toCompare, size)) 
                {// check() tests substring of first (size) letters
                    //printf("Here, if (check(pswd, toCompare))\n");
                    //printf("start: %s size %d\n", offset, size);
                    return true;
                }
             } 
        }
        return false;
    }
}


void* crack_handler(void* data)
{
//    printf("crack_handler\n");
    param* p = (param*) data;
    pwd_crack* task_out = malloc(sizeof(pwd_crack));
    task_out->to_compare = malloc(64);
    
    sem_wait(&p->task_in_queue->sem);
    pthread_mutex_lock(&p->task_in_queue->lock);
    pwd_crack* task = (pwd_crack*)front(p->task_in_queue);
    task_out->pwd_len = task->pwd_len;
    memcpy(task_out->start, task->start, 1);
    memcpy(task_out->to_compare, task->to_compare, strlen(task->to_compare));
    remove_front(p->task_in_queue, free_worker_pwd);
    pthread_mutex_unlock(&p->task_in_queue->lock);

    task_out->pwd_result = malloc(task_out->pwd_len);  
    task_out->is_found = false;
    uint8_t* pld = malloc(MAX_MSG_SIZE); 
    
    if ( is_task_correct(task) )
    {

//        printf("ready to crack: task start[%s] size[%d] target[%s]\n", 
               // task_out->start, task_out->pwd_len, task_out->to_compare);
        if ( crack(task_out->start, 0, task_out->pwd_len, task_out->to_compare, task_out->pwd_result) )
        {
            task_out->is_found = true;
 //           printf("///////----------Got cracked, <%s>\n", task_out->to_compare);
        }

        memset(pld, 0, MAX_MSG_SIZE);
        serialize_pld(task_out, pld);
 //       printf("send to server pld: [%s]\n", pld);
    }


    lsp_client_write(p->worker, pld, strlen(pld) );


    free(task_out->pwd_result);
    free(task_out->to_compare);
    free(task_out);
    free(pld);
}


void* task_handler(list* task_in_queue, uint8_t* pld)
{   
    //printf("task_handler\n");
    
    pwd_crack* task = malloc(sizeof(pwd_crack));

    parse_pld(pld,task);
//    printf("recv from server: pld[%s], to_compare [%s]", pld, task->to_compare);
    enqueue(task_in_queue, task);
}


int main(int argc, char *argv[]) 
{
    srand(12345);
    if(argc<2)
    {
        printf("Input format should be host:port\n");
        exit(1);
    }
    
    
    char* buf = malloc(128);
    strcpy(buf,argv[1]);
    char* host = malloc(64);
    char* phost = host;
    char* str_port = malloc(64);
    char* p = buf;
    if(!strstr(buf,":"))
        printf("cant find ':'");
    
    while(*p != ':')
        *phost++ = *p++;
    *phost = '\0';
    
    strcpy(str_port,strstr(buf,":")+1);
    int port = atoi(str_port);
    
    lsp_set_epoch_lth(2);
    lsp_set_epoch_cnt(5);
    lsp_set_drop_rate(0.01); 
    
//    printf("port = [%d] host = [%s]",port,host);
    
    //int port = SERVER_PORT;

    lsp_client* worker = lsp_client_create(host,port);
    if(!worker)
    {
        printf("Can't connect to server.\n");
        return 1;
    }
    lsp_client_write(worker, "j", sizeof("j"));
    
    
    
    //printf("---------------\n");
    uint8_t* pld = malloc(MAX_MSG_SIZE);
    
    list* task_in_queue = create_list(); 
    int count = 0;
    while(1)
    {       
        memset(pld, 0, MAX_MSG_SIZE);
     
        if(lsp_client_read(worker, pld)==-1)
        {
            printf("Can't recv message.\n");
            lsp_client_close(worker);
            worker = lsp_client_create(host, port);
            if(!worker)
            {
                printf("Can't connect to server.\n");
                return 1;
            }
            lsp_client_write(worker, "j", sizeof("j"));

            continue;
        }
        


        if ( strlen(pld) > HASH_PWD_SIZE )
        {
            printf("Error: Message Exceed Max Buffer Size!");
            continue;
        }
        
//      memset(pld, 0, MAX_MSG_SIZE);
  //      printf("pld from server: [%s]\n", pld);
        task_handler ( task_in_queue,  pld ); 
        
        param* p= (param*)malloc (sizeof(param));
        p->worker = worker;
        p->task_in_queue = task_in_queue;
            
        pthread_t id;    
        pthread_create (&id, NULL, crack_handler, (void*)p );
        pthread_detach(id);
     } 
    
 //   printf("out of loop\n");
    free(pld);
    lsp_client_close(worker);
    return 0;
}