#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "threadpool.h"
#include <libgen.h>
#include <signal.h>
#include <string.h>
#include "http_conn.h"
#include <sys/epoll.h>


#define MAX_FD 65535	// 最大的文件描述符个数/最多有多少客户端
#define MAX_EVENT_NUM 10000		// 最大监听的事件个数

// 添加信号捕捉
void AddSig(int sig, void(*handler)(int)) {
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	sigfillset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

extern void AddEpollFd(int epfd, int fd, bool one_shot);
extern void DelEpollFd(int epfd, int fd);
extern void ModEpollFd(int epfd, int fd, int ev);

int main(int argc, char* argv[]) {
	
	if (argc <= 1) {
		printf("run server using commond: %s port_number...\n", basename(argv[0]));
		exit(-1);
	}

	int port = atoi(argv[1]);

	AddSig(SIGPIPE, SIG_IGN);

	Threadpool<HttpConn> *pool = NULL;
	try {
		pool = new Threadpool<HttpConn>;
	}
	catch (...) {
		exit(-1);
	}

	// 保存所有客户端信息
	HttpConn* users = new HttpConn[MAX_FD];

	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1) {
		printf("create listen socket error...\n");
		exit(-1);
	}

	// 设置端口复用
	int reuse = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

	struct sockaddr_in saddr;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(port);
	saddr.sin_family = AF_INET;
	int ret = bind(lfd, (struct sockaddr*)&saddr, sizeof(saddr));
	if (ret == -1) {
		printf("bind error...\n");
		exit(-1);
	}

	ret = listen(lfd, 8);
	if (ret == -1) {
		printf("listen error...\n");
		exit(-1);
	}

	// 创建epoll对象，事件数组
	epoll_event events[MAX_EVENT_NUM];
	int epfd = epoll_create(10);
	if (epfd == -1) {
		printf("epoll_create error...\n");
		exit(-1);
	}

	epoll_event ep_event;
	ep_event.data.fd = lfd;
	ep_event.events = EPOLLIN | EPOLLRDHUP;
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ep_event);
	//AddEpollFd(epfd, lfd, false);
	HttpConn::epoll_fd_ = epfd;

	while (true) {
		int event_num = epoll_wait(epfd, events, MAX_EVENT_NUM, -1);
		//	如果是因中断导致的错误会返回错误号EINTR，此时不需要终止程序
		if ((event_num < 0) && (errno != EINTR)) {
			printf("epoll_wait error\n");
			break;
		}

		for (int i = 0; i < event_num; i++) {
			int cur_fd = events[i].data.fd;
			if (cur_fd == lfd) {
				sockaddr_in caddr;
				socklen_t len = sizeof(caddr);
				int cfd = accept(lfd, (sockaddr*)&caddr, &len);
				if (cfd == -1) {
					printf("accept error, %s\n", strerror(errno));
					continue;
				}
				
				if (HttpConn::user_count_ >= MAX_FD) {
					/*char* msg = "The server is busy now...Please try again later...\n";
					send(cfd, msg, strlen(msg), 0);*/
					close(cfd);
					continue;
				}
				char ip_buf[16];
				inet_ntop(AF_INET, &caddr.sin_addr.s_addr, ip_buf, sizeof(ip_buf));
				printf("client connect : %s\n", ip_buf);
				users[cfd].Init(cfd, caddr);
			}
			else if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
				//	对方异常断开
				users[cur_fd].CloseConn();
			}
			else if (events[i].events & EPOLLIN) {
				if (users[cur_fd].Read()) {
					pool->AppendTask(users + cur_fd);
				}
				else {
					users[cur_fd].CloseConn();
				}
			}
			else if (events[i].events & EPOLLOUT) {
				if (!users[cur_fd].Write()) {
					users[cur_fd].CloseConn();
				}
			}
		}
	}

	close(epfd);
	close(lfd);
	delete[] users;
	delete pool;
	return 0;
}