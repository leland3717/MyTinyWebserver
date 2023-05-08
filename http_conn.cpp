#include "http_conn.h"

int HttpConn::epoll_fd_ = -1;
int HttpConn::user_count_ = 0;
 
const char* kResourceRoot = "/home/leland/projects/MyTinyWebserver/resource";

void SetNonBlocking(int fd) {
	int flag = fcntl(fd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flag);
}


void AddEpollFd(int epfd, int fd, bool one_shot) {
	epoll_event ep_event;
	ep_event.data.fd = fd;
	ep_event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;

	if (one_shot) {
		ep_event.events |= EPOLLONESHOT;
	}

	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ep_event);

	// 设置文件描述符非阻塞
	SetNonBlocking(fd);
}

void DelEpollFd(int epfd, int fd) {
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
	close(fd);
}

void ModEpollFd(int epfd, int fd, int ev) {
	epoll_event ep_event;
	ep_event.data.fd = fd;
	ep_event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
	epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ep_event);
}

void HttpConn::Init() {
	read_idx_ = 0;
	check_idx_ = 0;
	line_start_idx_ = 0;  
	check_state_ = CHECK_STATE_REQUESTLINE;

	method_ = GET;
	url_ = 0;
	version_ = 0;
	linger_ = false;
	content_len_ = 0;
	target_path_ = 0;
	file_mem_addr_ = 0;

	bzero(read_buf_, kReadBufSize);
}

void HttpConn::Init(int sock_fd, const sockaddr_in& addr) {
	sock_fd_ = sock_fd;
	address_ = addr;

	// 设置端口复用
	int reuse = 1;
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

	AddEpollFd(epoll_fd_, sock_fd, true);
	user_count_++;

	Init();
}

void HttpConn::CloseConn() {
	if (sock_fd_ != -1) {
		DelEpollFd(epoll_fd_, sock_fd_);
		sock_fd_ = -1;
		user_count_--;
	}
}

bool HttpConn::Read() {
	if (read_idx_ >= kReadBufSize) {
		return false; 
	}

	int bytes_read = 0;
	while (true) {
		bytes_read = recv(sock_fd_, read_buf_ + read_idx_, kReadBufSize - read_idx_, 0);
		if (bytes_read == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			} 
			return false;
		}
		else if (bytes_read == 0) {
			std::cout << "client closed..." << std::endl;
			return false;
		}
		read_idx_ += bytes_read;
	}
	std::cout << "data from client: \n" << read_buf_ << std::endl;
	return true;
}

bool HttpConn::Write() {
	std::cout << "write data once" << std::endl;
	return true;
}

HttpConn::HttpCode HttpConn::ProcessRead(char* text) {
	LineStatus line_state = LINE_OK;
	HttpCode parse_res = NO_REQUEST;

	char* cur_line = 0;
	// 从read_buf_中逐行取出数据进行解析,但遇到请求体时一次性解析所有内容
	while (((check_state_ == CHECK_STATE_CONTENT) && line_state == LINE_OK) 
		|| (line_state = ParseLine()) == LINE_OK) {
		// 获取一行数据，遇到\0结束
		cur_line = read_buf_ + line_start_idx_;
		switch (check_state_) {
			case CHECK_STATE_REQUESTLINE: {
				parse_res = ParseRequestLine(cur_line);
				// （待做）根据不同返回值做不同的判断
				if (parse_res == BAD_REQUEST) {
					return BAD_REQUEST;
				}
				break;
			}
			case CHECK_STATE_HEADER: {
				parse_res = ParseHeader(cur_line);
				if (parse_res == BAD_REQUEST) {
					return BAD_REQUEST;
				}
				// 只有请求头没有请求体
				else if (parse_res == GET_REQUEST) {
					return DoRequest();
				}
				
				break;
			}
			case CHECK_STATE_CONTENT: {
				parse_res = ParseContent(cur_line);
				if (parse_res == GET_REQUEST) {
					return DoRequest();
				}
				// 解析失败
				line_state = LINE_OPEN;
				break;
			}
			default: {
				return INTERNAL_ERROR;
			}
		}
		line_start_idx_ = check_idx_;
	}
	if (line_state == LINE_OPEN) {
		return NO_REQUEST;
	}
	else if (line_state == LINE_BAD) {
		return BAD_REQUEST;
	}

	return NO_REQUEST;
}

HttpConn::HttpCode HttpConn::ParseRequestLine(char* text) {
	// "GET / HTTP/1.1"
	url_ = strchr(text, '\t');
	*url_++ = '\0'; 
	if (strcasecmp(text, "GET") == 0) {
		method_ = GET;
	}
	else {
		return BAD_REQUEST;
	}

	version_ = strchr(url_, '\t');
	if (!version_) {
		return BAD_REQUEST;
	}
	*version_++ = '\0';
	if (strcasecmp(version_, "HTTP/1.1") != 0) {
		return BAD_REQUEST;
	}

	// 路径名可能为http://xxx.xxx.xxx.xxx/index.html，取出/index.html
	if (strncasecmp(url_, "http://", 7) == 0) {
		url_ += 7;
		url_ = strchr(url_, '/');
	}
	if (!url_ || url_[0] != '/') {
		return BAD_REQUEST;
	}

	check_state_ = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

HttpConn::HttpCode HttpConn::ParseHeader(char* text) {
	if (text[0] == '\0') {
		if (content_len_ != 0) {
			check_state_ = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		return GET_REQUEST;
	}

	// Host: 192.168.17.128:8080
	char* value = strchr(text, ':');
	if (!value) {
		return BAD_REQUEST;
	}
	*value = '\0';
	value += 2;

	char* key = text;
	if (strcasecmp(key, "Host") == 0) {
		host_ = value;
	}
	else if (strcasecmp(key, "Connection") == 0) {
		if (strcasecmp(value, "keep-alive") == 0) {
			linger_ = true;
		}
	}
	else if (strcasecmp(key, "Content-Length") == 0) {
		content_len_ = atoi(value);
	}

	return NO_REQUEST;
}

// 没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
HttpConn::HttpCode HttpConn::ParseContent(char* text) {
	if (read_idx_ >= (content_len_ + check_idx_))
	{
		text[content_len_] = '\0';
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

HttpConn::LineStatus HttpConn::ParseLine() {
	while (check_idx_ < read_idx_) {
		if (read_buf_[check_idx_] == '\r') {
			// 表示读到\r就没有数据了
			if (check_idx_ + 1 == read_idx_) {
				return LINE_OPEN;
			}
			if (read_buf_[check_idx_ + 1] == '\n') {
				read_buf_[check_idx_] = '\0';
				read_buf_[check_idx_ + 1] = '\0';
				check_idx_ += 2;
				return LINE_OK;
			}
			return LINE_BAD;
		}
		else if (read_buf_[check_idx_] == '\n') {
			if (check_idx_ > 1 && read_buf_[check_idx_ - 1] == '\r') {
				read_buf_[check_idx_ - 1] = '\0';
				read_buf_[check_idx_++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		check_idx_++;
	}
	return LINE_OPEN;
}

HttpConn::HttpCode HttpConn::DoRequest() {
	strcpy(target_path_, kResourceRoot);
	int len = strlen(kResourceRoot);	// strlen不包括空字符
	//strncpy(target_path_ + len, url_, strlen(url_));
	strncpy(target_path_ + len, url_, KFileNameLen - len - 1);

	if (stat(target_path_, &file_stat_) == -1) {
		return NO_RESOURCE;
	}

	// 判断访问权限
	if (!(file_stat_.st_mode & S_IROTH)) {
		return FORBIDDEN_REQUEST;
	}

	if (S_ISDIR(file_stat_.st_mode)) {
		return BAD_REQUEST;
	}

	int fd = open(target_path_, O_RDONLY);
	file_mem_addr_ = (char*)mmap(NULL, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

void HttpConn::Unmap() {
	if (file_mem_addr_) {
		munmap(file_mem_addr_, file_stat_.st_size);
		file_mem_addr_ = 0;
	}
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void HttpConn::Process() {
	// 解析http请求 
	HttpCode read_ret = ProcessRead(read_buf_);
	if (read_ret == NO_REQUEST) {
		ModEpollFd(epoll_fd_, sock_fd_, EPOLLIN);
		return;
	}

	// 生成响应（准备好数据）
}