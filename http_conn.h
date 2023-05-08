#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <sys/epoll.h>
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>


class HttpConn {
public:
	static int epoll_fd_;		// 所有socket上的事件都注册到同一个epoll
	static int user_count_;		// 统计用户数量
	static const int kReadBufSize = 2048;
	static const int kWriteBufSize = 1024;
	static const int KFileNameLen = 200;

	// HTTP请求方法，这里只支持GET
	enum Method {
		GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT
	};

	/*
		解析客户端请求时，主状态机的状态
		CHECK_STATE_REQUESTLINE:当前正在分析请求行
		CHECK_STATE_HEADER:当前正在分析头部字段
		CHECK_STATE_CONTENT:当前正在解析请求体
	*/
	enum CheckState { 
		CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT 
	};

	/*
		服务器处理HTTP请求的可能结果，报文解析的结果
		NO_REQUEST          :   请求不完整，需要继续读取客户数据
		GET_REQUEST         :   表示获得了一个完成的客户请求
		BAD_REQUEST         :   表示客户请求语法错误
		NO_RESOURCE         :   表示服务器没有资源
		FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
		FILE_REQUEST        :   文件请求,获取文件成功
		INTERNAL_ERROR      :   表示服务器内部错误
		CLOSED_CONNECTION   :   表示客户端已经关闭连接了
	*/
	enum HttpCode { 
		NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION 
	};

	// 从状态机的三种可能状态，即行的读取状态，分别表示
	// 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
	enum LineStatus { 
		LINE_OK = 0, LINE_BAD, LINE_OPEN 
	};

public:
	HttpConn() {}
	~HttpConn() {}
	void Process();
	

	void Init(int sock_fd, const sockaddr_in& addr);
	void CloseConn();
	bool Read();	// 非阻塞
	bool Write();	// 非阻塞
private:
	int sock_fd_;	// 该Http连接的socket
	sockaddr_in address_;	// 通信的socket地址
	char read_buf_[kReadBufSize];
	char write_buf_[kWriteBufSize];
	int read_idx_;		// 标识已经读取的字节数的下一个位置
	int check_idx_;		// 当前正在分析的字符在读缓冲区的位置
	int line_start_idx_;	// 当前正在解析的行的起始位置
	CheckState check_state_;	// 主状态机当前所处的状态

	// http请求报文信息
	Method method_;
	char* url_;		// 请求的文件
	char* version_;
	char* host_;
	bool linger_;
	int content_len_;
	char* target_path_;	// 请求的文件的完整绝对路径
	struct stat file_stat_;
	char* file_mem_addr_;	// 请求文件映射到内存空间的地址

	void Init();
	HttpCode ProcessRead(char* text);
	HttpCode ParseRequestLine(char* text);
	HttpCode ParseHeader(char* text);
	HttpCode ParseContent(char* text);

	LineStatus ParseLine();
	HttpCode DoRequest();
	void Unmap();
};

#endif