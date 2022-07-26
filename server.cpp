#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <iostream> 
#include <fstream> 
#include <signal.h>
//#include "threadpool.h"
#include "new_pool.h"
#include "fdcntl.h"
using namespace std;
//宏定义，是否是空格
#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: JZK_epoll http/1.1\r\n"
int epfd;

//每次收到请求，创建一个线程来处理接受到的请求，把client_sock转成地址作为参数传入pthread_create
void *accept_request(int client);

//处理post请求
int run_post(int );

//错误请求
void bad_request(int);

//读取文件
void cat(int, FILE *);

//错误输出
void error_die(const char *);

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int, char *, int);

//返回http头
void headers(int, int);

//没有发现文件
void not_found(int);

//GET，直接读取文件返回给请求的http客户端
int serve_file(int, const char *);

//开启tcp连接，绑定端口等操作
int startup(u_short *);

//如果不是Get或者Post，就报方法没有实现
void unimplemented(int);

void *accept_request(int client)
{  
    char buf[1024];
    memset(buf,' ',sizeof(buf));
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int is_dynamic = 0,flag=1;
    char *query_string = NULL;
    memset(method,' ',sizeof(method));
    memset(url,' ',sizeof(url));
    memset(path,' ',sizeof(path));
    //读http 请求的第一行数据（request line），把请求方法存进 method 中
    numchars = get_line(client, buf, sizeof(buf));
    //cout<<client<<"  第一行"<<buf;
    if(numchars==-1){
		return NULL;
	}
    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        //提取其中的请求方式
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';
 	if(strcasecmp(method, "jzk") == 0){
		unimplemented(client);
		removefd(epfd,client);
        return NULL;
    }
    //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {	
        unimplemented(client);
	    removefd(epfd,client);
        return NULL;
    }
    if (strcasecmp(method, "POST") == 0)
    {
        is_dynamic = 1;
    }
    i = 0;
    //跳过所有的空白字符(空格)
    while (ISspace(buf[j]) && (j < sizeof(buf)))
    {
        j++;
    }
    //然后把 URL 读出来放到 url 数组中
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';
    //将前面分隔两份的前面那份字符串，拼接在字符串htdocs的后面之后就输出存储到数组 path 中。相当于现在 path 中存储着一个字符串
    sprintf(path, "httpdocs%s", url);
    //如果 path 数组中的这个字符串的最后一个字符是以字符 / 结尾的话，就拼接上一个"index.html"的字符串。首页的意思
    if (path[strlen(path) - 1] == '/')
    {
        strcat(path, "post.html");
	    //strcat(path, "test.html");
    }
    if (!is_dynamic)
    {
        if (stat(path, &st) == -1)
        {
            //如果不存在，那把这次 http 的请求后续的内容(head 和 body)全部读完并忽略
            while ((numchars > 0) && strcmp("\n", buf))
            {   
                numchars = get_line(client, buf, sizeof(buf));
		        if(numchars==-1){
                    return NULL;
                }
            }
            //然后返回一个找不到文件的 response 给客户端
            not_found(client);
            removefd(epfd,client);
            return NULL;
        }    
        flag = serve_file(client, path);
    }
    else
    {      
        flag = run_post(client);
    }
    if(flag==1){
    	removefd(epfd,client);
    	//cout<<"主动 close .."<<client<<endl;
    }
    return NULL;
}



void bad_request(int client)
{
    char buf[1024];
    //发送400
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}


void cat(int client, FILE *resource)
{
    //发送文件的内容
    char buf[1024];
    //从文件文件描述符中读取指定内容
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        int q=send(client, buf, strlen(buf), 0);
        //if(q<=0)printf("1\n");
        fgets(buf, sizeof(buf), resource);
    }
}

void error_die(const char *sc)
{
    //基于当前的errno值，在标准错误上产生一条错误消息
    perror(sc);
    exit(1);
}

int run_post(int client){
    char buf[1024];
    memset(buf,' ',sizeof(buf));
    char user_name[255];
    char user_pass[255];
    char hobbyone[10];
    char hobby[40];
    int index;
    char addr[10];
    int numchars=1,content_length=-1;
    while ((numchars > 0) && strcmp("\n", buf)){ 
    //while (numchars > 0){   
        numchars = get_line(client, buf, sizeof(buf));
        buf[15]='\0';
        if(numchars==-1){
            return 0;
        }else if(strcasecmp(buf, "Content-Length:") == 0){
            content_length = stoi(&(buf[16]));
        }
    }
    if (content_length == -1)
    {
        bad_request(client);
        return 1;
    }
    recv(client,buf,content_length,0);
    buf[content_length]='\0';
    char*p;
    const char c[2] = "&";
    int i=0;
    p=strtok(buf,c);
    while(p!=NULL){
        if(i==0){
            p=p+5;
            strcpy(user_name,p);
        }else if(i==1){
            p=p+9;
            strcpy(user_pass,p);
        }else if(i==2){
            strcpy(hobbyone,p);
            int n=strlen(hobbyone);
            if(!strcmp("on", &hobbyone[n-2])){
                for(int j=0;j<n;j++){
                    if(hobbyone[j]=='='){
                        hobby[index++]=',';
                        break;
                    }else{
                        hobby[index++]=hobbyone[j];
                    }
                }
                i--;
            }else{
                ++i;
                strcpy(p,hobbyone);
                continue;
            }
        }else if(i==3){
            p=p+8;
            strcpy(addr,p);
            if(!strcmp("beijing", addr)){
                sprintf(addr,"北京");
            }else if(!strcmp("shanghai", addr)){
                sprintf(addr,"上海");
            }else if(!strcmp("nanjing", addr)){
                sprintf(addr,"南京");
            }
        }
        ++i;
        p = strtok(NULL, c);
    }
    if(index==0) sprintf(hobby,"你没有兴趣?");
    else hobby[--index]='\0';
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type:text/html\r\n\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<html><head><meta charset=\"UTF-8\"><title>post_data</title></head>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<body><h2> Your POST data:  </h2><ul>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<li>账号=%s</li>\r\n",user_name);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<li>密码=%s</li>\r\n",user_pass);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<li>爱好=%s</li>\r\n",hobby);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<li>位置=%s</li>\r\n",addr);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</ul></body></html>\r\n");
    send(client, buf, strlen(buf), 0);
    //cout<<"done\n";
    return 1;
}


//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int sock, char *buf, int size)
{  
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size - 1) && (c != '\n'))
    {	
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {   
            if (c == '\r')
            {   
                //偷窥一个字节，如果是\n就读走
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {   
                    //不是\n（读到下一行的字符）或者没读到，置c为\n 跳出循环,完成一行读取
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
	    else if(n<0){
            if (errno == EAGAIN || errno == EWOULDBLOCK){
                continue;
            }
            removefd(epfd,sock);
            return -1;
	    }else
        {	
		    removefd(epfd,sock); 
		    return -1;
        }
    }
    buf[i] = '\0';
    return(i);
}

//加入http的headers
void headers(int client, int length)
{
    char buf[1024];
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Length: %s\r\n",to_string(length).c_str());
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

//如果资源没有找到得返回给客户端下面的信息
void not_found(int client)
{   
    char buf[1024];
    //返回404
    sprintf(buf, "HTTP/1.1 404 NOT FOUND\r\n");
    //sprintf(buf, "HTTP/1.1 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Length: \r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>TTTT</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

//如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端
int serve_file(int client, const char *filename)
{   
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))
    {	
        numchars = get_line(client, buf, sizeof(buf));
	    if(numchars==-1){
            return 0;
        }
    }
    //打开文件
    resource = fopen(filename, "r");
    if (resource == NULL)
    {
        not_found(client);
    }
    else
    {	
		ifstream is;  
  		is.open (filename, ios::binary );  
        is.seekg (0, ios::end);  
  		int length = is.tellg();  
  		is.seekg (0, ios::beg);   
        //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
        headers(client, length);
        //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
        cat(client, resource);
    }
    fclose(resource);//关闭文件句柄
    return 1;
}

//启动服务端
int startup(u_short *port)
{
    int httpd = 0,option;
    struct sockaddr_in name;
    //设置http socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
    {
        error_die("socket");//连接失败
    }
    int fla = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &fla, sizeof(fla));
	struct linger tmp = {0, 1};
    setsockopt(httpd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    //如果传进去的sockaddr结构中的 sin_port 指定为0，这时系统会选择一个临时的端口号
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    {
        error_die("bind");//绑定失败
    }
    //最初的 BSD socket 实现中，backlog 的上限是5
    if (listen(httpd, 5) < 0)
    {
        error_die("listen");
    }
    return(httpd);
}

//如果方法没有实现，就返回此信息
void unimplemented(int client)
{
    char buf[1024];
    //发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.1 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}
int main(void)
{   signal(SIGPIPE, SIG_IGN);
    int server_sock = -1;
    u_short port = 8080;//默认监听端口号 port 为6379
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    //创建线程池
    NewThreadPool pool(10);
    server_sock = startup(&port);
    int nfds,fla=1;
    //生成用于处理accept的epoll专用的文件描述符
    epfd = epoll_create(5);
    struct epoll_event ev, events[10000];
    addfd(epfd, server_sock, false, 0); //LT
    printf("httpd running on port: %d\n", port);
    while(true)
    {
        nfds = epoll_wait(epfd, events, 10000, -1);
        for(int i = 0; i < nfds; i++)
        {
            //如果新检测到一个socket用户连接到了绑定的socket端口，建立新的连接
            if(events[i].data.fd == server_sock)
            {
                client_sock = accept(server_sock, (sockaddr*)&client_name, &client_name_len);
                setsockopt(client_sock, SOL_SOCKET, SO_REUSEADDR, &fla, sizeof(fla));
                if(client_sock < 0)
                {   sleep(0.01);
                    perror("client_sock < 0!");
		            continue;
                }
                //char* str = inet_ntoa(client_name.sin_addr);
                //printf("accept a connnection from %s!\n", str);
		        addfd(epfd, client_sock, true, 1);//ET
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {   
		        client_sock=events[i].data.fd;
                removefd(epfd,client_sock);
            } else if(events[i].events & EPOLLIN)
            {	
				client_sock=events[i].data.fd;
                //如果是已经连接的用户，并且收到数据，那么进行读入
		        pool.AddTask(std::bind(accept_request,client_sock));
            }
        }
    }
    close(server_sock);
    return(0);
}
