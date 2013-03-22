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

/*
 * server parameter
 */
#define SERVER_PORT     5401
#define BUFFER_LENGTH    100
#define FALSE              0
#define SERVER_NAME     "127.0.0.1"
#define NETDB_MAX_HOST_NAME_LENGTH 256
#define HASH_PWD_SIZE 128
/*
 * 1.read argv[] from command line
 *   argv[1]: port  argv[2]: hostName/address
 */

int main(int argc, char *argv[])
{
    srand(12345);
    
    if(argc<4)
    {
        printf("Input format should be host:port hash pswd_len\n");
        exit(1);
    }
    char* arg_buf = malloc(128);
    strcpy(arg_buf,argv[1]);
    char* host = malloc(64);
    char* phost = host;
    char* str_port = malloc(64);
    
    char* p = arg_buf;

    while(*p != ':')
        *phost++ = *p++;
    *phost = '\0';

    strcpy(str_port,strstr(arg_buf,":")+1);
    int port = atoi(str_port);
    char* hash = malloc(64);
    strcpy(hash,argv[2]);
    int len = atoi(argv[3]);
    
 //   printf("ip [%s] port [%d] hash [%s] len[%d]",host,port,hash,len);
    
    lsp_set_epoch_lth(2);
    lsp_set_epoch_cnt(5);
    lsp_set_drop_rate(0.01);
    
    /////////
    //int port = SERVER_PORT;
    //////////////
    lsp_client* client = lsp_client_create(host, port);
    if(!client)
    {
        printf("Can't connect to server.\n");
        return 1;
    }
//    int pwd_len = 4;
    uint8_t buf[1024] = {0};
    
//    uint8_t* test[3]  = {"F2DCB113A9901E381AF63627C5BD380CDEBD53E4",  //"fbkd"  
//                         "433F1ABB485DA06BD7FB293E1F0E559394D787A",  //"higm"
//                         "6BE6ACE26D711791A4D9E5E4DB2130327EF5F1F2"};//"dsrl"
//    int index = 0;
    for(int i=0;i<5;i++)
    {
//        index = i%2;
//        sprintf(buf,"ca\n%d\n%s", pwd_len, test[1] );
//        int msg_len = strlen(buf);
//        printf("msg_len: %d\n", msg_len);
        
        sprintf(buf,"ca\n%d\n%s", len, hash);
        
        if(!lsp_client_write(client, buf, strlen(buf)))
        {
            printf("Can't send message.\n");
            lsp_client_close(client);
            client = lsp_client_create(host, port);
            continue;          
        }
        else
        {    
            //receive result;
            //worker: "fa\n3\nHASH\nPWD" "xa\n3\HASH"
            
            uint8_t* result = malloc(MAX_MSG_SIZE);
            if(lsp_client_read(client, result)==-1)
            {
                printf("Can't recv message.\n");
                lsp_client_close(client);
                client = lsp_client_create(SERVER_NAME, port);
                if(!client)
                {
                    printf("Can't connect to server.\n");
                    return 1;
                }
                continue;
            }
            
                    
            if ( strlen(result) > HASH_PWD_SIZE )
            {
                printf("Error: Message Exceed Max Buffer Size!");
                continue;
            }

            if(result[0] == 'f')
            {
                char* pos1 = strstr(result,"\n");
                char* pos2 = strstr(pos1 + 1,"\n");
                char* pos3 = strstr(pos2 + 1,"\n");
                char* hash = malloc(40);
                char* pwd = malloc(16);
                memset(pwd,0,16);
                memset(hash,0,40);
                memcpy(hash, pos2 + 1, (pos3-1) - (pos2+1) +1);
                memcpy(pwd,pos3 + 1,len);
                pwd[len] = '\0';
  //              printf("Found: [%s] = [%s].\n",hash,pwd);
                printf("Found: %s \n",pwd);
                return 0;
            }
            else if(result[0] == 'x')
            {
                printf("Not Found\n",result);  
                return;
            }
            else if(result[0] == 'n')
            {
                printf("Server is not ready! Please retry later!\n");
                return;
            }
           
        }
    }
    printf("Crack fail. Please retry! \n");
}



