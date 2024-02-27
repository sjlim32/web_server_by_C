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
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;    /* uri가 정적 인지 = (1), 동적인지(0) 확인하기 위한 변수 */
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  
  Rio_readinitb(&rio, fd);                            /* &rio에 파일 디스크립터(fd)로 초기화 */
  Rio_readlineb(&rio, buf, MAXLINE);                  /* &rio에 버퍼(buf)의 값을 읽어옴 */
  printf("Request headers:\n");
  printf("%s", buf);                                  /* 버퍼의 내용을 출력 */
  /* string scanf : 스트링을 읽고, 그 스트링을 뒤의 인자들에게 저장함 */
  sscanf(buf, "%s %s %s", method, uri, version);      /* buf에 저장된 문자열을 Null('\0')을 기준으로 나누어 저장 */
  if (strcasecmp(method, "GET"))      /* strcsasecmp : method에 저장된 문자값을 뒤의 인자와 비교해서, 같으면 0 틀리면 1 */
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  
  /* &rio에서 헤더의 값을 읽음, 읽고 넘기는 이유 : tiny에선 request의 어떤 헤더도 사용하지 않기 때문에 스킵 */
  read_requesthdrs(&rio);

  /* parse_uri를 통해 uri를 분석함 
  1. uri 뒤에 cgi_bin이 있는지 확인함
  2. (is_static == 1) cgi_bin이 없으면, 리턴값으로 1을 보냄. 정적 컨텐츠이므로 filename과 cgiargs가 없음
  3. (is_static == -1) cgi_bin이 있으면, 뒤에 ?가 있는지 확인해서 query 값이 있는지 확인.
  3-1. query 값이 있으면 filename을 uri로, query 값을 cgiargs 로 분리함 
  3-2. query 값이 없으면 cgiargs를 빈 값으로 만듦 
  */
  is_static = parse_uri(uri, filename, cgiargs);

  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static)
  {
    if (!S_ISREG(sbuf.st_mode) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  else 
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny coudln't run the CGI program");

      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

/* 
 ? *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-* Function *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-* ?
 */

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n"))    /* rp로 받아온 &rio의 포인터가 \r\n, 즉 헤더의 끝에 닿을 때까지 while문을 통해 한 줄씩 읽음*/
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  /* strstr(str arg1, str arg2) : arg1 에 arg2 가 존재하면 0, 존재하지 않으면 -1 */
  /* cgi-bin이 uri에 존재하지 않을 때 = 정적 페이지 */
  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");    /* filename과 home.html을 연결 */
    return 1;
  }
  /* cgi-bin이 존재하는 uri일 때, = 동적 페이지 */
  else
  {
    ptr = index(uri, '?');              /* query_string이 존재하는지 확인 */
    if (ptr) {
      strcpy(cgiargs, ptr + 1);         /* cgiargs에 query_string으로 넘어온 값을 저장 */
      *ptr = '\0';                      /* ptr을 Null로 만들어줌 */
    }
    else
      strcpy(cgiargs, "");              /* cgiargs에 Null을 저장. 즉, 인자값이 전달되지 않음 */

    strcpy(filename, ".");
    strcat(filename, uri);              /* . 과 /cgi-bin/adder를 연결하여 './cgi-bin/adder'로 만듦 */
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 요청에 대한 헤더를 클라이언트에 전달 */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* 요청에 대한 바디를 클라이언트에 전달*/
  srcfd = Open(filename, O_RDONLY, 0);
  /* CSAPP 과제 11.9 */
  srcp = (char *)malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);   /* 메모리에 srcfd의 값을 매핑함 */
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))      /* CSAPP 과제 11.7 */
    strcpy(filetype, "video/mp4");  
  else
    strcpy(filetype, "terxt/plance");  
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* HTTP 요청에 대한 처리 결과의 헤더 보내줌 */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0)
  {
    /* env 파일에 "QUERY_STRING"을 키값으로 하는 cgiargs를 저장, 1은 기존의 키값에 덮어쓰기를 허용함 */
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);    /* 현재 서버가 실행된 프로세스(즉, 부모)의 fd와 똑같은 fd목록을 생성 */
    Execve(filename, emptylist, environ);
    /* 
      filename의 파일을 실행(새 프로세스 생성), 
      emptylist는 execve가 요구하는 매개변수를 채우기 위함(빈 값으로 전달),
      environ은 현재 폴더의 환경변수를 새 프로세스에 전달
    */
  }
  Wait(NULL);
}

/* 에러 출력 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* HTTP 바디 생성 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""fffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* HTTP 응답 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}