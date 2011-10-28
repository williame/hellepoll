/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef TASK_HPP
#define TASK_HPP

#include "error.hpp"
#include "time.hpp"
#include "callback_list.hpp"
#include "out.hpp"

#include <unistd.h>
#include <sys/epoll.h>
#include <vector>
#include <algorithm>
#include "valgrind/memcheck.h"

class Readable {
public:
	virtual bool async_read(void* ptr,ssize_t bytes,ssize_t& read) = 0;
	virtual bool async_read(void* ptr,ssize_t bytes) = 0;
	virtual bool async_read(ResizeableBuffer& in,ssize_t& read,ssize_t max = 0) = 0;
	virtual bool async_read_str(char* s,size_t& len,size_t max) = 0;
	bool async_read_str(char* s,size_t max) { size_t len = strlen(s); return async_read_str(s,len,max); }
};

class Writeable {
public:
	virtual void async_write(const void* ptr,size_t len) = 0;
	virtual void async_write(Out* out) /* releases when sent */ = 0;
	virtual void async_write(const char* s) = 0;
	virtual void async_write_cpy(const void* ptr,size_t len) = 0;
	template<typename T> void async_write_t(const T& t,size_t bytes) {
		async_write_cpy(&t,bytes);
	}
};

bool starts_with(const char* s,const char* prefix);
bool ends_with(const char* s,const char* suffix,size_t suffix_len = 0);

template<int MAX> class InLine {
public:
	enum { max=MAX };
	friend class Task;
	InLine() { clear(); }
	void clear() { len = bufz[0] = 0; }
	char* cstr() { return bufz; }
	const char* cstr() const { return bufz; }
	inline bool starts_with(const char* prefix,size_t prefix_len = 0) const {
		prefix_len = prefix_len || strlen(prefix);
		return (prefix_len>len? false: !memcmp(prefix,bufz,prefix_len));
	}
	inline bool ends_with(const char* suffix,size_t suffix_len = 0) const {
		suffix_len = suffix_len || strlen(suffix);
		const char* tail = bufz+len-suffix_len;
		return (suffix_len>len? false: !memcmp(suffix,tail,suffix_len));
	}
	size_t size() const { return len; }
private:
	char bufz[MAX+1];
	size_t len;
};

class Scheduler;
typedef int FD;

class Task: virtual public ErrorContext, virtual public Closeable, virtual protected Readable, virtual protected Writeable {
public:
	friend class Scheduler;
	void construct();
	virtual ~Task();
	uint64_t gettid() const { return tid; }
	friend class Out;
	virtual void dump_context(FILE* out) const;
	uint32_t get_bytes_written() const { return totalWritten; }
	uint32_t get_bytes_read() const { return totalRead; }
	virtual void close();
	bool is_closed() const;
	static void popen(FD fd[3],const char *const cmd[]);
	// allow per-task logging overrides; system logging controlled by error.hpp Log()/SetLog()
	bool Log(LogLevel level);
	void SetLog(LogLevel level,bool enable);
	void setReadAheadBufferSize(uint16_t size);
protected:
	Task(Scheduler& scheduler,Task* parent = NULL);
	void set_nonblocking();
	static void set_nonblocking(int fd);
	void set_nodelay(bool enabled);
	static void set_nodelay(int fd,bool enabled);
	void set_cloexec();
	static void set_cloexec(FD fd);
	FD getfd() const { return fd; }
	void close_fd();
	bool is_end_of_input_stream() const { return eoinput; }
	void set_read_timeout(uint32_t millisecs); // 0 to clear
	void set_write_timeout(uint32_t millisecs); 
	virtual void handle_timeout(const time64_t& now);
	void schedule(uint32_t flags);
	void unschedule(uint32_t flags);
	// implementing Readable
	bool async_read(void* ptr,ssize_t bytes,ssize_t& read);
	bool async_read(void* ptr,ssize_t bytes);
	bool async_read_str(char *s,size_t& len,size_t max);
	using Readable::async_read_str;
	template<class InLine> bool async_read_in(InLine& in,size_t max = InLine::max);
	bool async_read(ResizeableBuffer& in,ssize_t& read,ssize_t max = 0);
	uint16_t async_read_buffered(uint8_t*& ptr,uint16_t max = ~0);
	// implementing Writeable
	void async_write(const void* ptr,size_t len);
	void async_write(Out* out) /* releases when sent */;
	void async_write(const char* s);
	void async_printf(const char* fmt,...);
	void async_vprintf(const char* fmt,va_list ap);
	void async_write_cpy(const void* ptr,size_t len);
private: // to be implemented/overriden by subclasses
	virtual void read() = 0;
	virtual void disconnected();
	virtual void do_construct() = 0;
protected:
	FD fd;
	Scheduler& scheduler;
	Out* out;
	const char* half_close;
private:
	static uint64_t nexttid();
	void run(uint32_t flags);
	bool async_write(const void* ptr,size_t len,size_t& written);
private:
	unsigned log, logMask;
	const uint64_t tid;
	epoll_event event;
	enum { MAX_BUF = 16 };
	char buf[MAX_BUF];
	ssize_t buflen;
	Task* next_close;
	bool del_ok;
	bool closed;
	bool eoinput;
	bool sated;
	uint32_t totalWritten;
	uint32_t totalRead;
	struct Link {
		Link();
		Task* prev;
		Task* next;
	} link;
	Task* tree_parent;
	Task* tree_first_child;
	Task* tree_next_sibling;
	struct Timeout: public Link {
		Timeout();
		time64_t due;
		struct Data {
			time64_t due;
			time64_t timeout;
		} read, write;
	} timeout;
	uint8_t* read_ahead_buffer;
	uint16_t read_ahead_ofs, read_ahead_len, read_ahead_maxlen;
private:
	void set_timeout(Timeout::Data& to,Timeout::Data& other,uint32_t millisecs);
	void unlink_timeout();
	void sort_timeout();
};

class Tick {
public:
	virtual time64_t tick(const time64_t& now) /* return time of scheduled next tick */ = 0;
};

class Scheduler: public ErrorContext {
public:
	Scheduler();
	~Scheduler();
	void run();
	bool is_shutting_down() const { return shutting_down; }
	FD getfd() const { return epoll_fd; }
	time64_t get_now() const { return now; }
	void enable_timeouts(bool enabled);
	void dump_context(FILE* out) const;
	const Task* get_current_task() const { return current_task; }
	friend class Task;
private:
	const int max_events;
	epoll_event* events;
	const FD epoll_fd;
	time64_t now;
	Task* current_task;
	Tick* tick;
	Task* close_list;
	Task* tasks;
	Task* timeouts;
	bool timeouts_enabled;
	bool shutting_down;
};

template<class InLine> bool Task::async_read_in(InLine& in,size_t max) {
	return async_read_str(in.bufz,in.len,std::min<size_t>(max,sizeof(in.bufz)));
}

#endif //TASK_HPP

