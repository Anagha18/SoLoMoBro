/*
Server Side code for ClusterCreate written by : Siddarth Karki, Karan Panjabi
15/03/2020
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <map>
#include <dirent.h>
#include <vector>
#include <algorithm>
#define PORT 8080
#define BUFFER_LEN 1024

using namespace std;

//structure definitions

typedef struct client_info{
  char ipAddr[25];
  int  port;
  int  sock_desc;
  int busy;
  pthread_cond_t cond1;
  pthread_mutex_t lock;
}client_info;

typedef struct params_connection_handler{
    int sd; //socket descriptor required for connecting to client. Don't disconnect
    int key;
    map<int, client_info> *client_table;
    map<int, string> *work_table;
    vector<string> *pending_files;
    vector<string> *completed_files;
}params_connection_handler;

typedef struct params_server_work{
    map<int, client_info> *client_table;
    map<int, string> *work_table;
    vector<string> *pending_files;
    vector<string> *completed_files;
}params_server_work;


//function declarations

void get_pending_files(vector<string> *);
void send_file(string , client_info&);
char* concatenate(char* , char*);
client_info* CreateClient(char*, int , int , int);
void print_client_details(map<int, client_info>);
void *connection_handler(void *);
void *start_server(void *);
void *distribute_work(void* params);


//function definitions

void get_pending_files(vector<string> *pending_files){
    DIR *directory;
    struct dirent *current;
    directory = opendir("../so_files/");
    if(directory){
      while((current = readdir(directory))!=NULL){
        string s(current->d_name);
        if(s.compare(s.length()-3, 3, ".so") == 0)
          pending_files->push_back(current->d_name);
      }
    }
    else{
      printf("The .so file directory doesn't exist!\n");
    }
}

void send_file(string pathstr, client_info& client){
    char* path = (char*)malloc(sizeof(char)*pathstr.length());
    strcpy(path, pathstr.c_str());
    char buffer[BUFFER_LEN];

    FILE *fp = fopen(path, "r");

    fseek(fp, 0, SEEK_END);
    int filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    for (int i = 0; i < filesize / BUFFER_LEN; i++){
        fread(buffer, BUFFER_LEN, 1, fp);
        write(client.sock_desc, buffer, BUFFER_LEN);

    }
    if (filesize%BUFFER_LEN != 0){
        int leftbytes = filesize - ftell(fp);
        fread(buffer, leftbytes, 1, fp);
        write(client.sock_desc, buffer, leftbytes);
    }
    fclose(fp);
}

char* concatenate(char* string1, char* string2){
  int i,j;
  char* result = (char*)malloc(sizeof(char)*(strlen(string1)+strlen(string2)));
  for(i=0;i<strlen(string1);i++){
    result[i] = string1[i];
  }
  for(j=0;j<strlen(string2);j++){
    result[i+j] = string2[j];
  }
  return result;
}

client_info* CreateClient(char* ipAddr, int port, int sock_desc, int busy){
  client_info *c = (client_info*)malloc(sizeof(client_info));
  strcpy(c->ipAddr, ipAddr);
  c->port = port;
  c->sock_desc = sock_desc;
  c->busy = busy;
  c->cond1 = PTHREAD_COND_INITIALIZER;
  c->lock = PTHREAD_MUTEX_INITIALIZER;
  return c;
}

void print_client_details(map<int, client_info> m){
  printf("Client Table :\n");
  for(auto x: m){
    printf("Client %d : %s:%d \n", x.first, x.second.ipAddr, x.second.port);
  }
}

void *connection_handler(void *socket_desc){
   //= (map<int, client_info> *)malloc(sizeof(map<int, client_info>));
   params_connection_handler *p = (params_connection_handler*)socket_desc;
   map<int, client_info> *client_table = p->client_table;
   map<int, string> *work_table = p->work_table;
   vector<string> *pending_files = p->pending_files;
   vector<string> *completed_files = p->completed_files;
   int sock = p->sd;
   int key = p->key;
   int read_size;
   char *message , client_message[2000];
   while(1){
     if(work_table->find(key)==work_table->end())
        pthread_cond_wait(&((*client_table)[key].cond1), &((*client_table)[key].lock));
     //this part is continued after the client recieves a signal from another thread when a  work is assigned.

     pthread_mutex_unlock(&((*client_table)[key].lock));
     write(sock , "ping" , strlen("ping"));
     read_size = recv(sock , client_message , 2000 , 0);
     if(read_size==0 || read_size== -1){
       printf("Client Disconnected\n");
       pending_files->push_back((*work_table)[key]);
       work_table->erase(key);
       client_table->erase(key);
       return NULL;
     }
     send_file((string("../so_files/")+(*work_table)[key]), (*client_table)[key] );
     //recieve file .....
     read_size = recv(sock , client_message , 2000 , 0);
     if((strcmp(client_message,"complete123")!=0 || read_size==0 || read_size==-1)){ //the files hasn't been sent properly back to server
         printf("Output file not collected properly\n");
         pending_files->push_back((*work_table)[key]);
         work_table->erase(key);
         if(read_size==0 || read_size==-1){
           client_table->erase(key);
          return NULL;
        }
     }
     else{
       printf("Output file collected succesfully from Client %d!\n", key);
       completed_files->push_back((*work_table)[key]);
       work_table->erase(key);
     }
   }
 }

void* start_server(void *params){
  params_server_work *p = (params_server_work*)params;
  map<int, client_info> *client_table = p->client_table;
  map<int, string> *work_table = p->work_table;
  vector<string> *pending_files = p->pending_files;
  vector<string> *completed_files = p->completed_files;

  int socket_desc , new_socket , c , *new_sock,i;
	struct sockaddr_in server , client;
	char *message;

	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1){
		printf("Sorry. The Socket could not be created!\n");
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);

  if( bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0)
   {
  		printf("Socket Binding Failed!\n");
	 }
  printf("Sever Socket has been binded to the port : %d\n",PORT);

	listen(socket_desc , 3);

	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);
  i = 0;
	while( (new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)) )
	{
		printf("Connection Accepted from: %s:%d (Client %d)\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port), i);
		pthread_t sniffer_thread;
    params_connection_handler *p = (params_connection_handler*)malloc(sizeof(params_connection_handler));
    p->key = i;
    p->sd = new_socket;
    p->client_table = client_table;
    p->work_table = work_table;
    p->pending_files  = pending_files;
    p->completed_files = completed_files;
    client_info *c = CreateClient(inet_ntoa(client.sin_addr), ntohs(client.sin_port), new_socket, 0);
    client_table->insert(pair<int, client_info> (i, *c));
    print_client_details(*client_table);

		if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) p) < 0){
			perror("could not create thread");
		}
    i++;
	}
	if (new_socket < 0){
		perror("accept failed");
	}
}

void* distribute_work(void* params){
  params_server_work *p = (params_server_work*)params;
  map<int, client_info> *client_table = p->client_table;
  map<int, string> *work_table = p->work_table;
  vector<string> *pending_files = p->pending_files;
  vector<string> *completed_files = p->completed_files;
  //you now have p->client_table, p->work_table, p->pending_files (two dicts and one vector respectively)
  int file_count = pending_files->size();
  while( completed_files->size()!=file_count ){ // till all the results of the given so files aren't accumilated
    for(auto x: *pending_files){ // iterate through the pending files list
      for(auto y: *client_table){ //get available client for the pending file by iterating through the client table and checking if a given client is busy
        if(!(y.second.busy)){ //if client is free assign pending file to it
          work_table->insert(make_pair(y.first, x));
          remove(pending_files->begin(), pending_files->end(), x);
          pthread_cond_signal(&(y.second.cond1));
        } //endif
      } //end clienttable iteration
    } //end pending file iteration
  }// end while
}//end of function

/******************************************************* MAIN FUNCTION ************************************************/
int main(int argc, char* argv[]){
  map<int, client_info> CLIENT_TABLE;
  vector<string> PENDING_FILES;
  vector<string> COMPLETED_FILES;
  map<int, string> WORK_TABLE;

  params_server_work *params = (params_server_work *)malloc(sizeof(params_server_work *));
  params->client_table = &CLIENT_TABLE;
  params->work_table = &WORK_TABLE;
  params->pending_files = &PENDING_FILES;
  params->completed_files = &COMPLETED_FILES;

  get_pending_files(&PENDING_FILES);

  //create two threads
  //start distrbute work thread after a delay of 5s just for a good demo


  return 0;
}
