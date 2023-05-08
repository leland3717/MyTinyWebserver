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
	static int epoll_fd_;		// ����socket�ϵ��¼���ע�ᵽͬһ��epoll
	static int user_count_;		// ͳ���û�����
	static const int kReadBufSize = 2048;
	static const int kWriteBufSize = 1024;
	static const int KFileNameLen = 200;

	// HTTP���󷽷�������ֻ֧��GET
	enum Method {
		GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT
	};

	/*
		�����ͻ�������ʱ����״̬����״̬
		CHECK_STATE_REQUESTLINE:��ǰ���ڷ���������
		CHECK_STATE_HEADER:��ǰ���ڷ���ͷ���ֶ�
		CHECK_STATE_CONTENT:��ǰ���ڽ���������
	*/
	enum CheckState { 
		CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT 
	};

	/*
		����������HTTP����Ŀ��ܽ�������Ľ����Ľ��
		NO_REQUEST          :   ������������Ҫ������ȡ�ͻ�����
		GET_REQUEST         :   ��ʾ�����һ����ɵĿͻ�����
		BAD_REQUEST         :   ��ʾ�ͻ������﷨����
		NO_RESOURCE         :   ��ʾ������û����Դ
		FORBIDDEN_REQUEST   :   ��ʾ�ͻ�����Դû���㹻�ķ���Ȩ��
		FILE_REQUEST        :   �ļ�����,��ȡ�ļ��ɹ�
		INTERNAL_ERROR      :   ��ʾ�������ڲ�����
		CLOSED_CONNECTION   :   ��ʾ�ͻ����Ѿ��ر�������
	*/
	enum HttpCode { 
		NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION 
	};

	// ��״̬�������ֿ���״̬�����еĶ�ȡ״̬���ֱ��ʾ
	// 1.��ȡ��һ���������� 2.�г��� 3.���������Ҳ�����
	enum LineStatus { 
		LINE_OK = 0, LINE_BAD, LINE_OPEN 
	};

public:
	HttpConn() {}
	~HttpConn() {}
	void Process();
	

	void Init(int sock_fd, const sockaddr_in& addr);
	void CloseConn();
	bool Read();	// ������
	bool Write();	// ������
private:
	int sock_fd_;	// ��Http���ӵ�socket
	sockaddr_in address_;	// ͨ�ŵ�socket��ַ
	char read_buf_[kReadBufSize];
	char write_buf_[kWriteBufSize];
	int read_idx_;		// ��ʶ�Ѿ���ȡ���ֽ�������һ��λ��
	int check_idx_;		// ��ǰ���ڷ������ַ��ڶ���������λ��
	int line_start_idx_;	// ��ǰ���ڽ������е���ʼλ��
	CheckState check_state_;	// ��״̬����ǰ������״̬

	// http��������Ϣ
	Method method_;
	char* url_;		// ������ļ�
	char* version_;
	char* host_;
	bool linger_;
	int content_len_;
	char* target_path_;	// ������ļ�����������·��
	struct stat file_stat_;
	char* file_mem_addr_;	// �����ļ�ӳ�䵽�ڴ�ռ�ĵ�ַ

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