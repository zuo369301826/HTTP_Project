#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define MAX 1024
#define HOME_PATH "index.html"
static int epoll_fd = 0;

//##################################################
//############       通用函数       ################
//##################################################
//获取首行
int get_line(int sock, char* line, int size)
{
  int i = 0;
  char c = 'a';
  while(i < size && c != '\n')
  {
    int s = recv(sock, &c, 1 , 0);
    if( s <= 0 )
      break;
    if(c == '\r')
    {
      recv(sock, &c, 1, MSG_PEEK);
      if(c != '\n')
      {
        c = '\n';
      }
      else
      {
        recv(sock, &c, 1, 0);
      }
    }
    line[i++] = c;
  }
  line[i] = '\0';
  return i;
}

//epoll添加文件描述符
void epoll_add(int fd)
{
  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

void clear_header(int sock)
{
  char buf[MAX];
  do{
    get_line(sock, buf, sizeof buf);
  }while(strcmp(buf, "\n") != 0);
}

//##################################################
//############     业务处理函数     ################
//##################################################
int exe_cgi(int sock, char* path, char* method, char* query_string)
{
  //运行前，获取到 method query_string content_length
  char line[MAX];
  int content_length = -1;

  char method_env[MAX/32] = {0};
  char query_string_env[MAX] = {0};
  char content_length_env[MAX] = {0};

  
  //GET 清空body  
  //POST 取content-length
  if(strcasecmp(method , "GET") == 0)
  {
    clear_header(sock);
  }
  else 
  {
    do
    {
      get_line(sock, line, sizeof line);
      if(strncmp(line, "Content-Length: ",16) == 0)
      {
        sscanf(line, "Content-Length: %d", &content_length);
      }
    }while(strcmp(line, "\n") != 0);
    
    if(content_length == -1)
      return 404;
  }

  //开始构造 首行和头部
  sprintf(line, "HTTP/1.1 200 OK\r\n");
  send(sock, line, strlen(line), 0);
  
  sprintf(line, "Content-type: text/html\r\n");
  send(sock, line, strlen(line), 0);

  sprintf(line, "\r\n");
  send(sock, line, strlen(line), 0);


  //开始执行程序
  int input[2];
  int output[2];
  
  pipe(input);
  pipe(output);

  pid_t pid = fork();
  if(pid < 0)
  {
    return 404;
  }

  if(pid == 0)
  {//子进程
    //因为子进程要进行进程替换，因此需要设置环境变量
    
    close(input[1]);
    close(output[0]);
    dup2(input[0], 0);
    dup2(output[1], 1);
    
    sprintf(method_env, "METHOD=%s", method);
    putenv(method_env);
  
    if(strcasecmp(method, "POST") == 0)
    {
      sprintf(content_length_env, "CONTENT_LENGTH=%d", content_length);
      putenv(content_length_env);
    }
    else 
    {
      sprintf(query_string_env, "QUERY_STRING=%s", query_string);
      putenv(query_string_env);
    }

    execl(path, path , NULL);
  }
  else 
  {//父进程
    //如果是post 开始获取body
    close(input[0]);
    close(output[1]);

    char c;
    if(strcasecmp(method, "POST") == 0)
    {
      int i = 0;
      for(; i<content_length; ++i)
      {
        read(sock, &c, 1);
        write(input[1], &c, 1);
      }
    }

    while( read(output[0], &c, 1) > 0)
    {
      send(sock, &c, 1, 0);
    }
    waitpid(input[1] , 0, 0);
    close(input[1]);
    close(output[0]);
  }
  return 200;
}

void echo_www(int sock, char* path, int size, int* errCode)
{
  clear_header(sock);
  char line[MAX];

  int fd = open(path, O_RDONLY);
  if( fd < 0)
  {
    *errCode = 0; 
    return ;
  }

  sprintf(line, "HTTP/1.1 200 OK\r\n");
  send(sock, line, strlen(line), 0);

  sprintf(line, "Content-Length: %d\r\n",size);
  send(sock, line, strlen(line), 0);
  
  sprintf(line, "\r\n");
  send(sock, line, strlen(line), 0);

  sendfile(sock, fd, NULL, size);
  
  close(fd);
}


//##################################################
//############     请求处理函数     ################
//##################################################
void ProcessCreate(int sock)
{
  struct sockaddr_in addr;
  socklen_t len = sizeof addr;
  int conn_sock =  accept(sock, (struct sockaddr*)&addr, &len);
  if(conn_sock < 0)
  {
    perror("accept");
    return ;

  }
  epoll_add(conn_sock);
  printf("连接成功：[id]: %d  [ip]:%s\n",conn_sock,inet_ntoa(addr.sin_addr)); 
}
      
void ProcessConnect(int sock)
{
  char first_line[MAX] = {0};
  char method[MAX/32] = {0};
  char url[MAX] = {0};
  char path[MAX] = {0};
  char* query_string = NULL;
  int errCode = 200;
  int cgi = 0;

  unsigned i = 0;
  unsigned j = 0;
 
  //日志
  printf("--------------------------------------------------------\n");
  printf("[%d] connect\n\n", sock);

  //1. 获取首行
  if(get_line(sock, first_line, sizeof(first_line)) ==  0 )
  {
    errCode = 401;
    goto end;
  }

  //2. 在首行中获取方法和url
  //2.1获取方法
  while(i < sizeof(method) -1 && j < sizeof(first_line) && !isspace(first_line[j]))
  {
      method[i++] = first_line[j++];
  }
  method[i] = '\0';
  while(j < sizeof(first_line) && isspace(first_line[j]))
  {
    j++;
  }
  i = 0;
  while(i < sizeof(url) -1 && j < sizeof(first_line) && !isspace(first_line[j]))
  {
      url[i++] = first_line[j++];
  }

  //3. 判断方法,
  //   如果是POST方法，不需要出去query_string
  //   执行CGI程序，如果是GET方法，就获得query_string
  //   根据方法判断是cgi程序的有两种情况：
  //      1. POST 方法就是cgi程序
  //      2. GET  方法存在query_string时，为cgi程序 
  if(strcasecmp(method, "POST") == 0)
  {
    cgi = 1;
  }
  else if(strcasecmp(method, "GET") == 0 )
  {
    query_string = url;
    while(*query_string)
    {
      if(*query_string == '?')
      {
        *query_string = '\0';
        query_string++;
        cgi = 1;
        break;
      }
      query_string++; 
    }
  }
  else 
  {
    //其他方法处理
    errCode = 402;
    goto end;
  }

  //4. 获取文件路径path,确保路径结尾不是'/'
  sprintf(path, "wwwroot%s", url);
  if( path[strlen(path)-1] == '/'  )
  {
    strcat(path, HOME_PATH);
  }

  //5. 获取文件信息
  struct stat st;
  if(stat(path, &st) < 0)
  {
    errCode = 403;
    goto end;
  }
  if(S_ISDIR(st.st_mode))
  {
    //如果是文件,加上默认文件路径
    strcat(path, HOME_PATH);
  }
  if( (st.st_mode & S_IXUSR) ||  (st.st_mode & S_IXGRP ) ||  (st.st_mode & S_IXOTH) )
  {
    //如果是可执行文件
    cgi = 1;
  }
  if(cgi)
  {
    printf("[GCI] method: %s, path: %s\n", method, path);
    errCode = exe_cgi(sock, path, method, query_string);
  }
  else 
  {
    printf("method: %s, path: %s\n", method, path);
    echo_www(sock, path, st.st_size, &errCode);
  }

end:
  if( errCode != 200 )
  {
    
    printf("%d\n",errCode);//TODO
  }
  close(sock);
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock, NULL);

  //日志
  printf("\n[%d] disconnect\n", sock);
  printf("--------------------------------------------------------\n");
}

// 套接字初始化
int SocketInit(char* ip, int port)
{
  //建立TCP客户端
  int sock = socket(AF_INET,SOCK_STREAM, 0);
  if( sock < 0 )
  {
    perror("socket");
    exit(1);
  }

  //设置端口可重用，解决time_wait时端口被占用的情况
  //要在绑定前设置
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

  //进行端口号绑定
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);
  if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0 )
  {
    perror("bind");
    exit(1);
  }

  //监听文件描述符
  if(listen(sock, 5) < 0)
  {
    perror("listen");
    exit(1);
  }

  // 建立 epoll 文件描述符
  epoll_fd = epoll_create(10);

  return sock;
}

//##################################################
//############     main 函数        ################
//##################################################

int main(int argc, char* argv[])
{
  if(argc != 3)
  {
    printf("./http [ip] [prot]\n");
    exit(1);
  }
  
  //初始化网络套接字
  int lis_sock = SocketInit(argv[1],atoi(argv[2]) );
 
  printf("sock create success...\n\n");
  //添加文件描述符到epoll
  epoll_add(lis_sock);

  struct epoll_event event[10];
  for(;;)
  {
    int size = epoll_wait(epoll_fd, event, sizeof(event)/sizeof(event[0]), 500);
    //有文件描述符就绪
    if(size < 0)
    {
      perror("epoll_wait");
      exit(1);
    }
    if(size == 0)
    {
      //超时
      continue; 
    }

    int i = 0;
    for(; i<size; ++i)
    {
      if(event[i].data.fd == lis_sock)
        ProcessCreate(lis_sock);
      else
        ProcessConnect(event[i].data.fd); 
    }
  }
  return 0;
}
