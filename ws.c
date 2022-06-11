#include "ws.h"
#include "sock.h"
#include "rio.h"
#include "syn.h"
#include "urlcode.h"
int valuepos;
char testtmp[10]="         ",value[MAXLINE][6];  //定义一个全局变量以交换请求头数据
void *thread(void *vargp );    /* 进程*/
void doit(int connfd );        /* 回馈 */
void read_requesthdrs(rio_t *rp);     /* 处理头 */
void get_filetype(char *filename, char *filetype);    /* 获取MIME类型 */
int parse_url(char *url, char *filename, char *cgiargs);     /* parse URL */
void serve_static(int fd, char *filename, int filesize);     /* 静态服务*/
void serve_dynamic(int fd, char *filename, char *cgiargs);   /* 动态服务*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);    /* error response */
void serve_dynamicpost(int fd, char *filename, char *cgiargs);

sbuf_t sbuf;     /* 连接缓冲区 */

int main(int argc, char *argv[] )
{
    int listenfd, connfd, port;
    unsigned int clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;
    int portused=1;
    int ret, i;    /* return-value, index */

    if(argc != 2){    /* 如果没有两个参数的话*/
        fprintf(stderr, "用法: %s <port>\n", argv[0]);    //未设置端口
        exit(1);
    }
    port = atoi(argv[1]);    /* 格式化 atoi是将char变整形 */
    
    sbuf_init(&sbuf, SBUFSIZE);    /* 头部缓冲区 */
    listenfd = open_listenfd(port);   /* 打开接收端口 */

    for(i = 0; i < NTHREADS; i++){   /* 多线程*/
        ret = pthread_create(&tid, NULL, thread, NULL);
        if(ret != 0){
            fprintf(stderr,  "create worker thread %d failed. \n", i);
        }
    }
    while(1){

        if(connfd==-1)  portused++;
        connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);   /* get request from client by IPv4 network */
        if(connfd!=-1){
            printf("client (%s:%d) has established connection, and connfd is %d\n",
                   inet_ntoa(clientaddr.sin_addr),    /* 用户ip地址*/
                   ntohs(clientaddr.sin_port), connfd);    /* 端口*/
            sbuf_insert(&sbuf, connfd);
        }else{
            printf("port %d is in used\n",port);
            sleep(1);
        }
        if(portused==4) exit(0);
    }

    return 0;
}

void *thread(void *vargp)   /* thread routine */
{
    pthread_detach(pthread_self());   /* 自动释放进程 */
    while(1){
        int connfd = sbuf_remove(&sbuf);     /* remove connfd from buffer */
        doit(connfd);                       /* serve client */
        close(connfd);                      /* close connfd */
        printf("connfd %d has closed.\n", connfd);
    }
}

void doit(int fd)     /* support service through fd */
{
	/*
	这里有一些很坑爹的地方要注意
	1.strcasecmp函数比较两个字符 如果不一样就是true 一样就是false
	*/
    int is_static;   /* 判断是否为静态网页 */
    struct stat sbuf;   /* file stat */
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE],bufs[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    char c;
    char cgipost[MAXLINE];
    int content_length=-1;
    rio_t rio;    /* 读取io*/
    int numchars = 1;
    rio_readinitb(&rio, fd);    /* init RIO buffer */
//    printf("%s\n",buf);
    rio_readlineb(&rio, buf, MAXLINE );  
    sscanf(buf, "%s %s %s", method,url,version);    
   //  printf("%s", buf); 调试服务器接收信息用


    if( strcasecmp(method, "GET")&&strcasecmp(method, "POST") ){    /* method is not GET ? */
        clienterror(fd, method, "501", "Not implemented", "CTL server does not implement this method except GET and POST");

      return ;
    }
    read_requesthdrs(&rio);   /* 处理请求头*/
    /* 处理url*/
    is_static = parse_url( url, filename, cgiargs );
    /* 返回值*/
    if( stat(filename, &sbuf) < 0 ){  
        clienterror( fd, filename, "404", "Not Found",
                   "CTL server could't find this file"); //404无文件
        return ;
    }
    if(is_static){   /* 是否为静态网页*/
        if( !(S_ISREG( sbuf.st_mode )) || !(S_IRUSR & sbuf.st_mode) ){  /* don't have permission? */
        clienterror( fd, filename, "403", "Forbidden",
                    "CTL server couldn't read this file");
             return ;
        }
        serve_static( fd, filename, sbuf.st_size);   /* 静态web*/
    }else{ /* 动态网页*/
        if( !(S_ISREG( sbuf.st_mode )) || !(S_IXUSR & sbuf.st_mode) ){  /* don't have permission? */
            clienterror( fd, filename, "403", "Forbidden",
                       "CTL server couldn't read this file");
            return ;
        }
        if( strcasecmp(method, "GET") ){
           // printf("%s",value[3]); //post长度 ftu
            content_length=atoi(value[valuepos]); //post长度格式化
//            printf("\n\n%s \n%s\n %s\n%s\n%s\n",value[0],value[1],value[2],value[3],value[4]);
            for (int i = 0; i < content_length; i++) {
                // 逐个读取
                rio_read(&rio, &c, 1);
//                printf("%c",c);//ftu 查看读取是否正常
                cgipost[i]=c;
               // printf("%s time=%d",cgipost,i);  //ftu
             }
	            printf("post-data=%s\n",cgipost);
            serve_dynamicpost( fd, filename, cgipost);
          //  clienterror( fd, filename, "501", "Sorry","CTL Server connot procces post request now"); //ftu 错误响应断点查看
           return ;
         }
        if( strcasecmp(method, "POST") ){    
             serve_dynamic( fd, filename, cgiargs);  /* 动态网页*/
        }
    }
}

void clienterror( int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

  /* 构建错误返回网页*/
    sprintf( body , "<html><title>CTL Server</title>");
    sprintf( body, "%s<body bgcolor=""#bfa"">\r\n", body);
    sprintf( body, "%s<h1>%s:%s</h1> \r\n", body, errnum, shortmsg);
    sprintf( body, "%s%s:%s:\r\n", body, longmsg, cause );
    sprintf( body, "%s<hr color=\"black\"><span style=\"font-size=20px;\">CTL server</span>\r\n", body);

   /* 输出请求头*/
    sprintf( buf, "HTTP/1.1 %s %s \r\n", errnum, shortmsg);
    rio_writen( fd, buf, strlen(buf) );
    sprintf( buf, "Content-Type: text/html\r\n");
    rio_writen(fd,buf, strlen(buf) );
    sprintf( buf, "Content-length: %ld\r\n\r\n", strlen(body) );
    rio_writen( fd, buf, strlen(buf) );

    /* 输出请求体*/
    rio_writen( fd, body, strlen(body) );
}


void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE], key[MAXLINE];
    char *p;
    int i=0;
    rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")){   /*until null*/
        p = strchr(buf, ':');/*deal with the request head*/
        if(p){
            *p = '\0';
            sscanf(buf, "%s", key);
            sscanf(p+1, "%s", value[i]);
            printf("%d    %s : %s\n",i, key, value[i]);
            if(!strcasecmp(key, "Content-Length")) valuepos=i;/*记录post长度*/
            i++;
        }
        rio_readlineb(rp, buf, MAXLINE );    /* next line */
    }
    return  ;
}

int parse_url( char *url, char *filename, char *cgiargs)
{
    char *ptr;
    if( !strstr(url, "cgi-bin") ){    /* 是否为静态 */
        strcpy( cgiargs, "");   /* no arg */
        strcpy( filename, "./html");  /* defalt path*/
        strcat( filename, url);  /* */
        if( url[ strlen(url) -1 ] == '/')  /* home网页*/
            strcat( filename,"home.html");
        return 1;   /* 返回是静态网页 */
    }else{    /* 动态网页 */
       ptr = strchr( url, '?');  /* 找到问号的位置 */
        if( ptr ){   /* has arguments ? */
             strcpy( cgiargs, ptr+1 ) ;
            *ptr = '\0';
    }else{  /* no arg */
        strcpy(cgiargs, "");
    }
    strcpy( filename, ".");
    strcat( filename, url );
    return 0;   /* 没有静态网页*/
    }
}
void serve_static( int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* 反回数据头*/
    get_filetype( filename, filetype );
    sprintf( buf, "HTTP/1.1 200 OK \r\n" );
    sprintf( buf, "%sServer: a simple web server\r\n", buf );
    sprintf( buf, "%sContent-length: %d\r\n", buf, filesize );
    sprintf( buf, "%sContent-Type: %s\r\n\r\n", buf, filetype );
    rio_writen( fd, buf, strlen(buf) );

    /* 返回响应体 */
    srcfd = open( filename, O_RDONLY, 0);
    /* 虚拟ram */
    srcp = mmap( 0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0 );  close( srcfd );    /* close the fd */
    rio_writen( fd, srcp, filesize );
    munmap( srcp, filesize);
}

/* MIME类型指示*/    
void get_filetype(char *filename, char *filetype)
{
    if( strstr(filename, ".html") )     /* html */
        strcpy( filetype, "text/html" );
    else if( strstr(filename, ".htm") )     /* htm */
        strcpy( filetype, "text/htm" );
    else if(strstr(filename, ".xml"))
        strcpy(filetype, "application/xml");
    else if(strstr(filename, ".pgn"))
        strcpy(filetype, "image/pgn");
    else if( strstr( filename, ".jpg") )   /* jpg */
        strcpy( filetype, "image/jpeg");
    else if( strstr(filename, ".gif") )    /* gif */
        strcpy( filetype, "image/gif");
    else if(strstr(filename, ".ico"))
        strcpy(filetype, "image/x-icon");
    else if(strstr(filename, ".pdf"))
        strcpy(filetype, "application/pdf");
    else  /* others */
        strcpy( filetype, "text/plain");
}
void serve_dynamic( int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist [] = {NULL};
    extern char **environ;
        sprintf( buf, "HTTP/1.1 200 OK\r\n");    /*状态码*/
    rio_writen( fd, buf, strlen(buf) );
    sprintf( buf, "Server: CTL server \r\n");  
    rio_writen( fd, buf, strlen(buf));
    sprintf( buf, "Content-Type: text/html\r\n");
    rio_writen(fd,buf, strlen(buf) );
    if( fork() == 0 ){   /* 子进程 */
	   setenv( "QUERY_STRING", cgiargs, 1);/*设置环境变量*/
 	   dup2( fd, STDOUT_FILENO);   /* redirect stdout to client */
   	   execve( filename, emptylist, environ);   /* run CGI program */
    }
    wait(NULL);    /*等待子进程结束 */
}

void serve_dynamicpost( int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist [] = {NULL};
    extern char **environ;
        sprintf( buf, "HTTP/1.1 200 OK\r\n");    /*状态码*/
    rio_writen( fd, buf, strlen(buf) );
    sprintf( buf, "Server: CTL server \r\n");
    rio_writen( fd, buf, strlen(buf));
    sprintf( buf, "Content-Type: text/html\r\n");
    rio_writen(fd,buf, strlen(buf) );
    if( fork() == 0 ){   /* 子进程 */
           setenv( "QUERY_STRING", cgiargs, 1);/*设置环境变量*/
           setenv("Content_Length",value[valuepos],1);
           dup2( fd, STDOUT_FILENO);   /* redirect stdout to client */
           execve( filename, emptylist, environ);   /* run CGI program */
    }
    wait(NULL);    /*等待子进程结束 */
}
