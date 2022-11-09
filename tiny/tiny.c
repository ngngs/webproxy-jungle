/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;                                        // 클라이언트 소켓 길이
  struct sockaddr_storage clientaddr;                         // 클라이언트 소켓 주소

  /* Check command line args */
  if (argc != 2) {                                            // argc는 default로 1개가 있다. port number를 입력하지 않았다면, error
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);                          // 해당 port number에 해당하는 listen 소켓 식별자 열어주기 
  while (1) {                                                 // 요청이 들어올 때마다 호출(무한 서버 루프)
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 서버연결 식별자(Accept함수는 요청 대기 함수) // 그림 11.14참고

    // 클라이언트 소켓에서 hostname과 port number를 string으로 변환
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    doit(connfd);   // html 한 번 호출, gif 호출해서 총 2번 실행
    Close(connfd);  // 서버연결 식별자를 닫아주면 하나의 트랜잭션 끝
  }
}

// 클라이언트의 요청 라인을 확인해, 정적, 동적 컨텐츠를 구분하고 각각의 서버에 보냄
void doit(int fd){    // connfd가 인자로 들어옴
  int is_static;      // 정적파일 판단해주기 위한 변수
  struct stat sbuf;   // 파일 정보를 가진 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;


  /* Read request line and headers */
  Rio_readinitb(&rio, fd);                        // rio 버퍼와 fd(여기서는 서버의 connfd)를 연결
  Rio_readlineb(&rio, buf, MAXLINE);              // buf에서 client request 읽기
  printf("Request headers:\n");
  printf("%s", buf);                              // request header 출력
  sscanf(buf, "%s %s %s", method, uri, version);  // buf에 있는 데이터를 method, uri, version에 담기

  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)){                 // strcasecmp함수가 같으면 0을 출력, method가 GET이 아니라면 error message 출력
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);                         // 요청 헤더를 무시하지만, 뭐가 들어왔는지는 보여주는 함수

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs);  // parse_uri : 클라이언트 요청 라인에서 받아온 uri를 이용해(정적이면 1, 동적이면 0)
  if (stat(filename, &sbuf) < 0){                 // filename에 맞는 정보 조회 하지 못하면 error message 출력                   
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static){                                                 // request file이 static contents면 실행
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {  // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;

    }
    serve_static(fd, filename, sbuf.st_size, method);             // static contents라면, 사이즈를 같이 서버에 보낸다 
  }
  else {                                                          // request file이 dynamic contents면 실행
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){   // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);                 // dynamic contents라면, 인자를 같이 서버에 보낸다
  }
}

// error 발생 시, client에게 보낼 response(error message)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  // HTTP 형식으로 response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  
  // response 작성
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));

  // buf와 body를 서버 소켓을 통해 클라이언트에게 보냄
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// tiny는 요청 헤더 내의 어떤 정보도 사용하지 않고 이를 무시
void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);


  while(strcmp(buf, "\r\n")){   // 빈 텍스트 줄이 아닐 때까지 읽기
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// uri parsing을 받아 요청받은 filename, cgiargs로 채움
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

  // static file request인 경우(uri에 cgi-bin이 없으면)
  // dynamic file은 cgi-bin 아래 저장하기로 했음
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");  // 현재 directory로 이동해라
    strcat(filename, uri);  // uri은 '/'로 시작해서 들어옴
    // request에서 어떤 static contents도 요구하지 않은 경우(home.html로 이동)
    if (uri[strlen(uri)-1] == '/'){
      strcat(filename, "home.html");
    }
    return 1;
  }
  // dynamic file request인 경우
  else {                            
    // uri부분에서 file name과 args를 구분하는 ?위치 찾기
    ptr = index(uri, '?');
    // '?'가 있으면
    if (ptr){
      // cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    // ?가 없으면
    else{
      strcpy(cgiargs, "");    // 위에 선언한 ptr을 찾지 못 했으므로 null
    }
    // filename에 uri 담기
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

// 클라이언트가 원하는 정적 컨텐츠를 받아와서 응답 라인과 헤더를 작성하고 서버에게 보냄
// 그 후 정적 컨텐츠 파일을 읽어 그 응답 바디를 클라이언트에게 보냄
void serve_static(int fd, char *filename, int filesize, char *method){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 클라이언트에게 응답 헤더 보내기

  // 응답 라인과 헤더 작성
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");                    // 응답라인 작성
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);     // 응답헤더 작성
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
 
  // 응답 라인과 헤더를 클라이언트에게 전송
  Rio_writen(fd, buf, strlen(buf));                        // connfd를 통해 clientfd에게 보냄
  printf("Response headers:\n");
  printf("%s", buf);                                       // 서버 측에서도 출력

  if (strcasecmp(method, "HEAD") ==0){
    return;
  }

  srcfd = Open(filename, O_RDONLY, 0);                     // filename의 이름을 갖는 파일을 읽기 권한으로 불러옴

  // Malloc
  srcp = malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);                           
  free(srcp);
  
  /* Mmap방법 : 파일의 메모리를 그대로 가상 메모리에 매핑함.
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);                           // 해당 메모리에 있는 파일 내용들을 fd에 보냄
  Munmap(srcp, filesize);                                   // Mmap 했으니까 Munmap(malloc하면 free 하듯)
  */
}

// get_filetype - Derive file type from filename
void get_filetype(char *filename, char *filetype){
  if (strstr(filename, ".html"))      strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))  strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))  strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))  strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))  strcpy(filetype, "video/mp4");
  else  strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // if문도 실행되기 때문에 Fork()는 무조건 실행됨
  if (Fork() == 0){ //Fork 가 0은 자식 프로세스가 있다는 것. 
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);
    // 클라이언트의 표준 출력을 CGI 프로그램의 표준출력과 연결
    // CGI 프로그램에서 printf하면 클라이언트에서 출력
    Dup2(fd, STDOUT_FILENO);              
    Execve(filename, emptylist, environ); 
  }
  Wait(NULL);                             
}