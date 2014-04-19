#include <iostream>
#include <string>
#include <queue>

#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>	//for atoi
#include <pthread.h>
#include <string.h> //for memcpy,memset...
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>


using namespace std;

#define PROXY_PORT 8888
#define MAX_THREADS 50
#define WHITE_SPACE ' '
#define RESPONSE_BUFFER_SIZE 8092

pthread_mutexattr_t mutex_attr;
pthread_mutex_t mutex;

queue<int> socket_buffer;

//==============================================================
//==============================================================
void run_server();
void *request_thread(void *args);
void get_target(const char* request,string &host,string &port);
void get_target_response(int threadId,const char *target_url,
	const char *target_port,const char *request,int sockfd);
bool is_connection_alive(const char *buffer,int len);

//==============================================================
//==============================================================

int main(int argc,char **argv)
{
	cout<<"Http Proxy Server Version 0.0.1"<<endl;
	cout<<"Develop by chaogu"<<endl;
	run_server();
	return 0;
}

//==============================================================
//Get host & port form request string
//==============================================================
void get_target(const char *request,string& host,string& port)
{
	host = "";
	port = "";
	
	//skip first HTTP command
	while(*request != ' '){
		++request;
	}
	
	//skip non character
	while((*request < 'a' || *request > 'z') && (*request < 'A' || *request > 'Z')){
		++request;
	}
	const char *url_start = request;
	/***************************************************
	cout<<request<<endl;
	if(	(*(request+0) == 'h' || *(request+0) == 'H') &&
		(*(request+1) == 't' || *(request+1) == 'T') &&
		(*(request+2) == 't' || *(request+2) == 'T') &&
		(*(request+3) == 'p' || *(request+3) == 'P') &&
		(*(request+4) == ':'))
	{
		request += 7;
		
		cout<<"won't throgth here"<<endl;
		cout<<"!!!!!!!!!!!!!!!!!!"<<endl;
		
		//skip white space
		while(*request == WHITE_SPACE){
			++request;
		}
		cout<<"111 ok"<<endl;
		while(*request != WHITE_SPACE && *request != '\n' &&
			*request != '\r' && *request != '\0' && *request != ':'){
			host += *request;
			++request;
		}
		cout<<"222 ok"<<endl;
		if(*request == ':'){
			while(*request != WHITE_SPACE && *request != '\n' &&
				*request != '\r' && *request != '\0' && *request != ':'){
				port += *request;
				++request;
			}
		}
		cout<<"333 ok"<<endl;
		if(port == ""){
			port = "80";
		}
		return;
	}
	***********************************************************/
	while(*request != '\0'){
		if(*request == '\n' && *(request + 1) == 'H' && *(request + 2) == 'o' &&
			*(request + 3) == 's' && *(request + 4) == 't' && *(request + 5) == ':'){
			request += 6;
			while((*request < 'a' || *request >'z') && (*request < 'A' || *request > 'Z')){
				++request;
			}
			
			//find host in "Host:"
			while(*request != WHITE_SPACE && *request != '\n' && *request != '\t' &&
				*request != '\r' && *request != '\0' && *request != ':'){
				host += *request;
				++request;
			}
			
			//find prot number in "Host:"
			if(*request == ':'){
				while(*request != WHITE_SPACE && *request != '\n' && *request != '\t' &&
					*request != '\r' && *request != '\0' && *request != ':'){
					port += *request;
					++request;
				}	
			}
			break;
		}
		++request;
	}
	
	if(host.length() > 0 && port.length() == 0){
		while(*url_start != WHITE_SPACE){
			if(*url_start == ':'){
				++url_start;
				while(*url_start != WHITE_SPACE && *url_start != '/' && *url_start != '\\')
					port += *url_start++;
				break;
			}
			++url_start;
		}

		if(port.length() == 0)
			port="80";

		return;
	}
}
//==============================================================
//Get response form host:port server
//==============================================================
void get_target_response(int threadId,const char *target_url,
	const char *target_port,const char *request,int sockfd)
{
	int request_sockfd;
	struct sockaddr_in client_addr;
	struct hostent *phost;
	bool keep_alive = false;
	
	//init sockfd & host
	request_sockfd = socket(AF_INET,SOCK_STREAM,0);
	phost = gethostbyname(target_url);
	memset(&client_addr,0,sizeof(struct sockaddr_in));
	if(phost == NULL){
		cout<<"get host name error"<<endl;
		return;
	}

	client_addr.sin_family = AF_INET;
	memcpy(&client_addr.sin_addr,phost->h_addr,phost->h_length);
	client_addr.sin_port = htons(atoi(target_port));
	if(connect(request_sockfd,(struct sockaddr *)&client_addr,
		sizeof(struct sockaddr_in)) == -1){
		cout<<"connect error"<<endl;
		close(request_sockfd);
		return;
	}
	//cout<<"1 ok"<<endl;
	//send request data to host:port
	if(send(request_sockfd,request,strlen(request),0) == -1){
		cout<<"Error when receiving"<<endl;
		return;
	}
	//cout<<"2 ok"<<endl;
	const int buffer_size = RESPONSE_BUFFER_SIZE;
	char recv_buffer[buffer_size];
	char send_buffer[buffer_size];
	memset(recv_buffer,0,buffer_size);
	memset(send_buffer,0,buffer_size);
	//cout<<"3 ok"<<endl;
	int nread = 0;
	nread = recv(request_sockfd,recv_buffer,buffer_size,0);
	keep_alive = is_connection_alive(recv_buffer,nread);
	//cout<<"4 ok"<<endl;
	if(keep_alive){
		do{
			cout<<"Thread "<<threadId<<"[Connection : keep-alive] : "<<nread<<endl;
			memcpy(send_buffer,recv_buffer,buffer_size);
			send(sockfd,send_buffer,nread,0);
			memset(send_buffer,0,buffer_size);
			nread = recv(request_sockfd,recv_buffer,buffer_size,0);
			if(nread <= 0){
				cout<<"recv error"<<endl;
				break;
			}
		}while(true);
	}else{
		do{
			cout<<"Thread "<<threadId<<"[Connection : close] : "<<nread<<endl;
			memcpy(send_buffer,recv_buffer,buffer_size);
			send(sockfd,send_buffer,nread,0);
			nread = recv(request_sockfd,recv_buffer,buffer_size,0);
			if(nread <= 0){
				cout<<"recv error"<<endl;
				break;
			}
		}while(true);
	}
	cout<<"close socket"<<endl;
	close(request_sockfd);
}

//==============================================================
//is this connection alive
//==============================================================
bool is_connection_alive(const char *buffer,int len)
{
	int count = 0;
	while(count < len && *buffer != '\0'){
		if(strncmp(buffer,"Connection:",strlen("Connection:")) == 0){
			buffer += strlen("Connection:") + 1; 	//skip "\nConnection:"
			while(*buffer == WHITE_SPACE)
				++buffer;							//skip white space
			if(strncmp(buffer,"Close",strlen("Close")) == 0)
				return false;						//close option 
			return true;
		}
		++buffer;
	}
	return true;									//can't find "Connection:"
}

//==============================================================
//thread function
//==============================================================
void* request_thread(void *args)
{
	int *tid = (int *)args;
	int threadId = *tid;
	while(true){
		int sockfd;
		pthread_mutex_lock(&mutex);
		
		if(socket_buffer.size() < 1){
			pthread_mutex_unlock(&mutex);
			continue;
		}
		
		//get an sockfd from socket_buffer in the front
		//must unlock the mutex even catch an excpetion
		try{
			sockfd = socket_buffer.front();
			socket_buffer.pop();
			pthread_mutex_unlock(&mutex);
		}catch(exception){
			pthread_mutex_unlock(&mutex);
			continue;
		}
		try{
			//the recive buffer
			const int buffer_size = 1024;
			char buffer[buffer_size];
			memset(buffer,0,buffer_size);
			string data = "";
		
			int recv_length = recv(sockfd,buffer,buffer_size - 1,0);
			if(recv_length <= 0){
				//no data recv
				return NULL;
			}
			data += buffer;	//get the buffer to the data<string>
			while(recv_length >= buffer_size -1){
				memset(buffer,0,buffer_size);
				recv_length = recv(sockfd,buffer,buffer_size - 1,0);
				data += buffer;
			}
		
			//get the request url:port
			string target_url = "";
			string target_port = "";
			get_target(data.c_str(),target_url,target_port);
		
			//get target response
			get_target_response(threadId,target_url.c_str(),
				target_port.c_str(),data.c_str(),sockfd);
			//clost the sockfd READ & WIRTE
			shutdown(sockfd,SHUT_RDWR);
			close(sockfd);
			cout<<"Thread "<<threadId<<" socket closed"<<endl;
		}catch(exception){
			//close sockfd anyway
			shutdown(sockfd,SHUT_RDWR);
			close(sockfd);
		}	
	}
}

void run_server()
{
	int server_sockfd,sockfd;
	struct sockaddr_in server_addr,client_addr;
	memset(&server_addr,0,sizeof(struct sockaddr_in));
	memset(&client_addr,0,sizeof(struct sockaddr_in));
	
	server_sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(server_sockfd == -1){
		cout<<"invalid socke"<<endl;
		return;
	}
	
	//init server addr
	bzero(&server_addr,sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PROXY_PORT);
	
	//bind the server_sockfd on server_addr
	if(bind(server_sockfd,(struct sockaddr *)&server_addr,sizeof(struct sockaddr_in)) == -1){
		cout<<"bind error"<<endl;
		return;
	}
	
	if(listen(server_sockfd,5) == -1){
		cout<<"listen error"<<endl;
		return;
	}
	
	//setup the server name
	char local_host_name[256];
	hostent *local_ent = NULL;
	in_addr local_addr;
	string host_ip = "";
	
	if(gethostname(local_host_name,sizeof(local_host_name)) == -1){
		memset(local_host_name,0,sizeof(local_host_name));
	}else{
		local_ent = gethostbyname(local_host_name);
		int i = 0;
		while(local_ent->h_addr_list[i]){
			if(i > 0){
				host_ip += ".";
			}
			memcpy(&local_addr,local_ent->h_addr_list[i],sizeof(local_addr));
			host_ip += inet_ntoa(local_addr);
			++i;
		}
	}
	
	cout<<"start proxy server at "<<host_ip<<":"<<PROXY_PORT<<endl;
	//init the mutex 
	pthread_mutexattr_init(&mutex_attr);
	if(pthread_mutex_init(&(mutex),NULL)){
		cout<<"mutex not created"<<endl;
		return;
	}
	
	//init threads
	int 			threads[MAX_THREADS];
	int				thread_args[MAX_THREADS];
	pthread_attr_t	thread_attr[MAX_THREADS];
	pthread_t		thread_ids[MAX_THREADS];
	
	for(int i = 0;i < MAX_THREADS;++i){
		thread_args[i] = i;
		pthread_attr_init(&thread_attr[i]);
		pthread_attr_setstacksize(&thread_attr[i],1024 * 120);
		threads[i] = pthread_create(&thread_ids[i],&thread_attr[i],request_thread,&thread_args[i]);
	}
	
	//server main loop
	while(true){
		sockfd = accept(server_sockfd,NULL,NULL);
		pthread_mutex_lock(&mutex);
		socket_buffer.push(sockfd);
		pthread_mutex_unlock(&mutex);
	}
	shutdown(server_sockfd,SHUT_RDWR);
	close(server_sockfd);
}
