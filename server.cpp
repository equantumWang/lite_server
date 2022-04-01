#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<errno.h>

#include "fdwrapper.h"
#include "util.h"
#include "InetAddress.h"

#define MAX_EVENTS 1024
#define READ_BUFFER 1024


int main() {
	printf("test0!");
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(listenfd == -1, "socket create error");

	const char* ip = {"127.0.0.2"};
    int port = 1234;
	printf("test!");
	InetAddress *addr = new InetAddress(ip, port);	//该处应与逻辑服务器的地址/端口相同
	socklen_t addr_len = addr->getAddr_len();
    struct sockaddr_in serv_addr = addr->getAddr();

    errif(bind(listenfd, (sockaddr*)&serv_addr, addr_len) == -1, "socket bind error");
    errif(listen(listenfd, SOMAXCONN) == -1, "socket listen error");
   
    int epfd = epoll_create1(0);
    errif(epfd == -1, "epoll create error");

    epoll_event events[MAX_EVENTS], ev;
    bzero(&events, sizeof(events));

	add_read_fd(epfd, listenfd);



	while (true) {
		int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
		errif(nfds == -1, "epoll wait error");

		for (int i = 0; i < nfds; ++i)
		{
			if (events[i].data.fd == listenfd)//新客户端连接
			{
				struct sockaddr_in clnt_addr;
				bzero(&clnt_addr, sizeof(clnt_addr));
				socklen_t clnt_addr_len = sizeof(clnt_addr);
				
				int clnt_sockfd = accept(listenfd, (sockaddr*)&clnt_addr, &clnt_addr_len);
					errif(clnt_sockfd == -1, "socket accept error");
				printf("new client fd %d! IP: %s Port: %d\n", clnt_sockfd, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
			
				// bzero(&ev, sizeof(ev));
				// ev.data.fd = clnt_sockfd;
				// ev.events = EPOLLIN | EPOLLET;
				// setnonblocking(clnt_sockfd);
				// epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sockfd, &ev);
				add_read_fd(epfd, clnt_sockfd);
			} 
			else if (events[i].events & EPOLLIN)	//可读事件
			{
				char buf[READ_BUFFER];
				while(true)
				{
					bzero(&buf, sizeof(buf));
					ssize_t bytes_read = read(events[i].data.fd, buf, sizeof(buf));
					if(bytes_read > 0)
					{
					printf("message from client fd %d: %s\n", events[i].data.fd, buf);
					write(events[i].data.fd, buf, sizeof(buf));
					} else if (bytes_read == -1 && errno == EINTR)//客户端正常中断，继续读取
					{
					printf("continue reading");
					continue;
					} else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))//非阻塞IO，代表数据读取完毕
					{
					printf("finish reading once, errno: %d\n", errno);
					break;
					} else if (bytes_read == 0){//EOF,客户端断开连接
					printf("EOF, client fd %d disconnected\n", events[i].data.fd);
					close(events[i].data.fd);	//关闭socket并自动将fd从epoll树上移除
					break;
					}
				}
			} 
			else
			{
				printf("something else happened\n");//其他事件，后续版本实现
			}
		}
    }
    close(listenfd);
    return 0;
}
