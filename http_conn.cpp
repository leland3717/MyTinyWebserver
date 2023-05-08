#include "http_conn.h"

int HttpConn::epoll_fd_ = -1;
int HttpConn::user_count_ = 0;

// 定义HTTP响应的一些状态信息
const char* kOkTitle_200 = "OK";
const char* kErrorTitle_400 = "Bad Request";
const char* kErrorInfo_400 = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* kErrorTitle_403 = "Forbidden";
const char* kErrorInfo_403 = "You do not have permission to get file from this server.\n";
const char* kErrorTitle_404 = "Not Found";
const char* kErrorInfo_404 = "The requested file was not found on this server.\n";
const char* kErrorTitle_500 = "Internal Error";
const char* kErrorInfo_500 = "There was an unusual problem serving the requested file.\n";

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
	file_mem_addr_ = 0;

	write_idx_ = 0;
	bytes_left_ = 0;
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
			printf("client closed...\n");
			return false;
		}
		read_idx_ += bytes_read;
	}
	return true;
}

bool HttpConn::Write() {
	int bytes_send = 0;

	while (1) {
		bytes_send = writev(sock_fd_, iv_, iv_count_);
		if (bytes_send == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				int total_send = write_idx_ + file_stat_.st_size - bytes_left_;
				if (total_send < write_idx_) {
					iv_[0].iov_base = (char*)iv_[0].iov_base + total_send;
					iv_[0].iov_len -= total_send;
				}
				else {
					// 或者改成iv_count_ = 1
					iv_[0].iov_len = 0;
					iv_[1].iov_base = file_mem_addr_ + total_send - write_idx_;
					iv_[1].iov_len = file_stat_.st_size - (total_send - write_idx_);
				}
				ModEpollFd(epoll_fd_, sock_fd_, EPOLLOUT);
				return true;
			}
			Unmap();
			return false;
		}

		bytes_left_ -= bytes_send;
		if (bytes_left_ == 0) {
			Unmap();
			if (linger_) {
				Init();
				ModEpollFd(epoll_fd_, sock_fd_, EPOLLIN);
				return true;
			}
			return false;
		}
	}

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
	url_ = strchr(text, ' ');
	*url_++ = '\0';
	if (strcasecmp(text, "GET") == 0) {
		method_ = GET;
	}
	else {
		return BAD_REQUEST;
	}

	version_ = strchr(url_, ' ');
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
	if (strcmp(url_, "/") == 0) {
		strncpy(target_path_ + len, "/index.html", kFileNameLen - len - 1);
	}
	else {
		strncpy(target_path_ + len, url_, kFileNameLen - len - 1);
	}
	printf("%s\n", target_path_);
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
	if (fd == -1) {
		return NO_RESOURCE;
	}

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

bool HttpConn::AddResponse(const char* format, ...) {
	if (write_idx_ > kWriteBufSize) {
		return false;
	}
	va_list vl;
	va_start(vl, format);
	int len = vsnprintf(write_buf_ + write_idx_, kWriteBufSize - write_idx_ - 1, format, vl);
	if (len >= kWriteBufSize - write_idx_ - 1) {
		return false;
	}
	write_idx_ += len;
	va_end(vl);
	return true;
}

bool HttpConn::AddStatusLine(int status, const char* title) {
	return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::AddHeaders(int content_len) {
	if (!AddContentLength(content_len)) {
		return false;
	}
	if (!AddContentType()) {
		return false;
	}
	if (!AddLinger()) {
		return false;
	}
	if (!AddBlankLine()) {
		return false;
	}
}

bool HttpConn::AddContentLength(int content_len) {
	return AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::AddContentType() {
	return AddResponse("Content-Type: %s\r\n", "text/html");
}

bool HttpConn::AddLinger() {
	return AddResponse("Connection: %s\r\n", (linger_ == true) ? "keep-alive" : "close");
}

bool HttpConn::AddBlankLine() {
	AddResponse("%s", "\r\n");
}

bool HttpConn::AddContent(const char* content) {
	AddResponse("%s", content);
}

bool HttpConn::ProcessWrite(HttpCode read_ret) {
	switch (read_ret) {
		case INTERNAL_ERROR: {
			AddStatusLine(500, kErrorTitle_500);
			AddHeaders(strlen(kErrorInfo_500));
			if (!AddContent(kErrorInfo_500)) {
				return false;
			}
			break;
		}
		case BAD_REQUEST: {
			AddStatusLine(400, kErrorTitle_400);
			AddHeaders(strlen(kErrorInfo_400));
			if (!AddContent(kErrorInfo_400)) {
				return false;
			}
			break;
		}
		case NO_RESOURCE: {
			AddStatusLine(404, kErrorTitle_404);
			AddHeaders(strlen(kErrorInfo_404));
			if (!AddContent(kErrorInfo_404)) {
				return false;
			}
			break;
		}
		case FORBIDDEN_REQUEST: {
			AddStatusLine(403, kErrorTitle_403);
			AddHeaders(strlen(kErrorInfo_403));
			if (!AddContent(kErrorInfo_403)) {
				return false;
			}
			break;
		}
		case FILE_REQUEST: {
			AddStatusLine(200, kOkTitle_200);
			AddHeaders(file_stat_.st_size);
			if (bytes_left_ == 0) {
				iv_[0].iov_base = write_buf_;
				iv_[0].iov_len = write_idx_;
				iv_[1].iov_base = file_mem_addr_;
				iv_[1].iov_len = file_stat_.st_size;
				iv_count_ = 2;
				bytes_left_ = iv_[0].iov_len + iv_[1].iov_len;
			}
			return true;
		}
		default: {
			return false;
		}
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
	bool write_ret = ProcessWrite(read_ret);
	if (!write_ret) {
		CloseConn();
	}
	ModEpollFd(epoll_fd_, sock_fd_, EPOLLOUT);
}