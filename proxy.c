#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
 *    Client                 Proxy(thread)                             Server   
 *  ----------                ----------                             ----------           
 * | Clientfd |   ------->   |  Connfd  |                           |  Connfd  |  
 *  ----------                ----------                             ----------
 * |          |              | Proxy    |          doit()           |          |
 * |          |              | Clientfd |        --------->         |          |
 * |          |              |          |    request_to_server()    | Listenfd |
 * |          |              | Listenfd |                           |          |
 * |          |   <-------   |          |        <---------         |          |        
 *  ----------    Close()     ----------   response_from_server()    ----------
 *                           | CacheMem |
 *                            ----------
 *                          
*/


// 프로토타입 선언
void doit(int fd);
void request_to_server(int proxy_clientfd, char *uri_to_s);
void response_from_server(int fd, int proxy_clientfd);
void *thread(void *vargsp);
int parse_uri(char *uri, char *port_to_s, char *hostname_to_s, char *uri_to_s);

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;                                        
  struct sockaddr_storage clientaddr;   
  pthread_t tid;                      

  if (argc != 2) {                                            
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // Proxy listenfd 
  listenfd = Open_listenfd(argv[1]);                           
  while (1) {                                                 
    clientlen = sizeof(clientaddr);

    // Client - Proxy(connfd)
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

    Pthread_create(&tid, NULL, thread, (void *)connfd);               // (&tid : 쓰레드 식별자, NULL : 쓰레드 속성)
  }

  return 0;
}

void *thread(void *vargs){
  int connfd = (int)vargs;    //arg로 받는 것을 connfd에 넣기
  Pthread_detach(pthread_self());
  doit(connfd);
  Close(connfd);
}

void doit(int fd){
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname_to_s[MAXLINE], port_to_s[MAXLINE], uri_to_s[MAXLINE];
  int proxy_clientfd;
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);                       
  sscanf(buf, "%s %s %s", method, uri, version);
  parse_uri(uri, port_to_s, hostname_to_s, uri_to_s);
  proxy_clientfd = Open_clientfd(hostname_to_s, port_to_s);
  request_to_server(proxy_clientfd, uri_to_s);
  response_from_server(fd, proxy_clientfd);

  Close(proxy_clientfd);
}

int parse_uri(char *uri, char *port_to_s, char *hostname_to_s, char *uri_to_s){
  char *ptr;
  
  // 'uri' 자르기                             // uri : http://www.cmu.edu/hub/index.html 
  if (!(ptr = strstr(uri, "://"))){
    return -1;
  }
  ptr += 3;
  strcpy(hostname_to_s, ptr);                 // hostname_to_s : www.cmu.edu/hub/index.html 

  // uri_to_s 찾기, hostname_to_s 자르기
  if ((ptr = strchr(hostname_to_s, '/'))){    
    *ptr = '\0';                              // hostname_to_s : www.cmu.edu
    ptr += 1;
    strcpy(uri_to_s, "/");
    strcat(uri_to_s, ptr);                    // uri_to_s : /hub/index.html 
  }
  else{ // '/'가 존재하지 않는 경우
    strcpy(uri_to_s, "/");
  }
  
  // 포트번호 찾기
  if ((ptr = strchr(hostname_to_s, ':'))){
    *ptr = '\0';
    ptr += 1;
    strcpy(port_to_s, ptr);
  }
  else{
    strcpy(port_to_s, "80");                    // port : 80
  }

  return 0;

}

void request_to_server(int proxy_clientfd, char *uri_to_s){
  char buf[MAXLINE];
  char version[] = "HTTP/1.0";
      
  sprintf(buf, "GET %s %s\r\n", uri_to_s, version);  
  sprintf(buf, "%s%s", buf, user_agent_hdr);              
  sprintf(buf, "%sConnections: close\r\n", buf);            
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);   

  Rio_writen(proxy_clientfd, buf, (size_t)strlen(buf));
}

void response_from_server(int fd, int proxy_clientfd){
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, proxy_clientfd);
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE);   
  Rio_writen(fd, buf, n);
}