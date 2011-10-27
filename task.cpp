/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "task.hpp"
#include <algorithm>
#include <set>
#include <sched.h>

//#define CHG_PRIO

extern "C" {
	#include <string.h>
	#include <fcntl.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/tcp.h>
	#include <netinet/in.h>
	#include <stdlib.h>
	#include <stdarg.h>
	#include <ctype.h>
	#include <sys/time.h>
	#include <sys/resource.h>
}

Scheduler::Scheduler(): max_events(1000), events(new epoll_event[1000]),
	epoll_fd(epoll_create1(EPOLL_CLOEXEC)), current_task(NULL), tick(NULL),
	close_list(NULL), tasks(NULL), timeouts(NULL), timeouts_enabled(true),
	shutting_down(false) {
	check(epoll_fd);
}

Scheduler::~Scheduler() {
	// close all tasks
	shutting_down = true;
	for(Task* task = tasks; task; task=task->link.next)
		if(!task->closed)
			task->close();
	while(close_list) {
		Task* tmp = close_list;
		close_list = close_list->next_close;
		tmp->del_ok = true;
		delete tmp;
	}
	close(epoll_fd);              
	delete[] events;
}

template<typename T> T min(const T& a,const T& b) {
	return (a<b? a: b);
}

void Scheduler::run() {
	const time64_t tick_interval = millisecs_to_time64(1000);
	time64_t next_tick = (time64_now() + tick_interval);
	while(tasks) {
		int timeout = -1; //infinite
		if(tick || timeouts) {
			now = time64_now();
			if(tick) {
				if(now >= next_tick)
					next_tick = min<time64_t>(tick->tick(now),1);
				timeout = time64_to_millisecs(next_tick-now);
			}
			if(timeouts_enabled) {
				while(timeouts && (now >= timeouts->timeout.due)) {
					assert(!timeouts->closed); // ignore half-closed, so don't use is_closed()
					timeouts->handle_timeout(now);
					timeouts->close(); // unlinks, and resets timeouts pointer
				}
				if(timeouts) {
					int next_timeout = time64_to_millisecs(timeouts->timeout.due-now);
					if(tick)
						timeout = min(timeout,next_timeout);
					else
						timeout = next_timeout;
				}
			}
		}
		//printf("ready... (%d)\n",timeout);

		int nfds;
		check(nfds = epoll_wait(epoll_fd,events,max_events,timeout));
		now = time64_now();
		for(int i=0; i<nfds; i++) {
			current_task = reinterpret_cast<Task*>(events[i].data.ptr);
			if(current_task->closed)
				continue;
			//current_task->dump_context(stdout);
			//printf("is running...\n");
			try {
				current_task->run(events[i].events);
				continue;
			} catch(Error* e) {
				if(current_task->Log(LOG_CRITICAL)) {
					e->dump(this);
					e->release();
				}
			} catch(std::exception& e) {
				if(current_task->Log(LOG_CRITICAL))
					fprintf(stderr,"std::exception: %s\n",e.what());
			} catch(Shutdown* sd) {
				fprintf(stderr,"shutting down: %s\n",sd->msg);
				// exit
				return;
			} catch(...) {
				current_task->dump_context(stderr);
				fprintf(stderr,"unexpected exception!\n");
			}
			current_task->close();
		}
		current_task = NULL;
		// delete those marked as closed
		while(close_list) {
			Task* tmp = close_list;
			close_list = close_list->next_close;
			tmp->del_ok = true;
			delete tmp;
		}
	}
}

void Scheduler::enable_timeouts(bool enabled) {
	timeouts_enabled = enabled;
}

void Scheduler::dump_context(FILE* out) const {
	fprintf(out,"Scheduler ");
	if(current_task)
		current_task->dump_context(out);
}

Task::Link::Link(): prev(NULL), next(NULL) {}

Task::Timeout::Timeout(): due(0) {
	read.due = write.due = 0;
}

Task::Task(Scheduler& s,Task* parent): fd(-1), scheduler(s), out(NULL), half_close(NULL),
	log(0U), logMask(0U),tid(nexttid()),
	buflen(0), next_close(NULL), del_ok(false), closed(false), eoinput(false),
	sated(true), totalWritten(0), totalRead(0),
	tree_parent(parent), tree_first_child(NULL), tree_next_sibling(NULL),
	read_ahead_buffer(0), read_ahead_ofs(0), read_ahead_len(0) {
	memset(&event,0,sizeof(event));
	event.data.ptr = this;
	event.events = 0;
	if(Log(LOG_CONN)) {
		dump_context(stdout);
		fprintf(stdout,"created\n");
	}
	// add to task tree
	if(tree_parent) {
		if(!tree_parent->tree_first_child)
			tree_parent->tree_first_child = this;
		else {
			Task* sibling = tree_parent->tree_first_child;
			while(sibling->tree_next_sibling)
				sibling = sibling->tree_next_sibling;
			sibling->tree_next_sibling = this;
		}
	}
}

Task::~Task() {
	assert(closed);
	assert(del_ok);
	// unlink it
	if(this == scheduler.tasks) {
		assert(!link.prev);
		scheduler.tasks = link.next;
	}
	if(link.prev)
		link.prev->link.next = link.next;
	if(link.next)
		link.next->link.prev = link.prev;
	free(read_ahead_buffer);
}

void Task::close_fd() {
	if(-1 == fd) // avoid spurious valgrind warnings if it's been closed by subclass explicitly
		return;
	unschedule(~0);
	::close(fd);
	fd = -1;
}

void Task::close() {
	if(closed)
		return;
	closed = true;
	while(out) {
		Out* tmp = out;
		out = out->next;
		tmp->release();
	}
	close_fd();
	for(Task* child = tree_first_child; child; child = child->tree_next_sibling)
		child->close(); // cascade close all children
	if(Log(LOG_CONN)) {
		dump_context(stdout);
		fprintf(stdout,"~ closed\n");
		fflush(stdout);
	}
	if(Task* parent = tree_parent) {
		while(parent->tree_parent)
			parent = parent->tree_parent;
		parent->close(); // cascade from the very top too
	}
	next_close = scheduler.close_list;
	scheduler.close_list = this;
	unlink_timeout();
	fd = -1;
}

void Task::construct() {
	Cleanup<Task,CleanupClose> self(this);
	// link it in
	if(scheduler.tasks) {
		link.next = scheduler.tasks;
		link.next->link.prev = this;
	}
	scheduler.tasks = this;
	// virtual construction
	do_construct();
	// check and go
	assert(0<fd && "expecting to be assigned an FD");
	assert(event.events && "expecting to be scheduled");
	set_nonblocking();
	set_cloexec();
	self.detach();
	if((EPOLLET & event.events) && (EPOLLIN & event.events))
		run(EPOLLIN); // consume any already-received input; ### put on a run-list instead?
}

void Task::setReadAheadBufferSize(uint16_t size) {
	if(read_ahead_buffer) {
		// tidy it up
		read_ahead_len -= read_ahead_ofs;
		read_ahead_ofs = 0;
		memmove(read_ahead_buffer,read_ahead_buffer+read_ahead_ofs,read_ahead_len);
		// somethig to do?
		if(read_ahead_len > size)
			ThrowInternalError("truncating the read-ahead buffer would lose %d buffered bytes",read_ahead_len);
		// resize it
		if(size) {
			if(uint8_t* tmp = (uint8_t*)realloc(read_ahead_buffer,size)) {
				read_ahead_buffer = tmp;
				read_ahead_maxlen = size;
			} //else log it failed?
		} else {
			free(read_ahead_buffer);
			read_ahead_buffer = NULL;
		}
	} else if(size) {
		read_ahead_ofs = 0;
		read_ahead_len = 0;
		read_ahead_maxlen = size;
		read_ahead_buffer = (uint8_t*)malloc(size);
	}
}

static void DebugTaskTotals(Task& task,uint32_t prevWritten,uint32_t prevRead) {
	if(task.Log(LOG_DEBUG)) {
		const uint32_t written = (task.get_bytes_written() - prevWritten), read = (task.get_bytes_read() - prevRead);
		if(written || read) {
			task.dump_context(stdout);
			fprintf(stdout,"DEBUG");
			if(written)
				fprintf(stdout," %"PRIu32" written",written);
			if(read) {
				if(written)
					fputc(',',stdout);
				fprintf(stdout," %"PRIu32" read",read);
			}
			fputc('\n',stdout);
		}
	}
}

void Task::disconnected() {
	ThrowClientError("disconnected");
}

void Task::run(uint32_t flags) {
	const uint32_t prevWritten = totalWritten, prevRead = totalRead;
	try {
		if((EPOLLHUP|EPOLLRDHUP|EPOLLERR)&flags) {
			eoinput = true;
			disconnected();
			close_fd();
			return;
		}
		if(~(EPOLLIN|EPOLLOUT)&flags)
			ThrowInternalError("unexpected event");
		if(!half_close && (EPOLLIN&flags)) {
			if(timeout.read.due)
				timeout.read.due = (scheduler.get_now() + timeout.read.timeout);
			try {
				sated = false;
				read();
				if(!sated && (EPOLLET & event.events) && (EPOLLIN & event.events))
					ThrowInternalError("not sated");
			} catch(HalfClose* hc) {
				sated = true;
				if(!out)
					throw;
				hc->dump(this);
				hc->release();
				unschedule(EPOLLIN);
				/*check(*/shutdown(fd,SHUT_RD)/*)*/;
				half_close = hc->msg;
			}
			sated = true;
		}
		if(EPOLLOUT&flags) {
			if(timeout.write.due)
				timeout.write.due = (scheduler.get_now() + timeout.write.timeout);
			while(out) {
				if(!out->async_write(this))
					break;
				Out* tmp = out;
				out = out->next;
				tmp->release();
			}
			if(!out) {
				unschedule(EPOLLOUT);
				if(half_close)
					ThrowGracefulClose(half_close);
			}
		}
	} catch(...) {
		DebugTaskTotals(*this,prevWritten,prevRead);
		throw;
	}
	DebugTaskTotals(*this,prevWritten,prevRead);
	if(timeout.read.due || timeout.write.due) {
		// nothing out, so don't care about write timeout?
		if(timeout.write.due && !out) {
			timeout.due = timeout.read.due;
			if(!timeout.due) // no read scheduled either
				unlink_timeout();
		} else {
			timeout.due =
				timeout.read.due?
					timeout.write.due?
						std::min(timeout.read.due,timeout.write.due):
						timeout.read.due:
					timeout.write.due;
		}
		sort_timeout();
	}
}

bool Task::Log(LogLevel level) {
	if(logMask & level)
		return (log & level);
	return ::Log(level); // else defer to system defaults
}

void Task::SetLog(LogLevel level,bool enable) {
	logMask |= level;
	if(enable)
		log |= level;
	else
		log &= ~level;
}

void Task::sort_timeout() {
	// move to the right place in the queue, O(n)			
	while(timeout.next && (timeout.next->timeout.due < timeout.due)) {
		assert(this == timeout.next->timeout.prev);
		if(this == scheduler.timeouts)
			scheduler.timeouts = timeout.next;
		if(timeout.prev) {
			assert(this == timeout.prev->timeout.next);
			timeout.prev->timeout.next = timeout.next;
		}
		timeout.next->timeout.prev = timeout.prev;
		timeout.prev = timeout.next;
		timeout.next = timeout.prev->timeout.next;
		timeout.prev->timeout.next = this;
		if(timeout.next)
			timeout.next->timeout.prev = this;
	}
	while(timeout.prev && (timeout.prev->timeout.due > timeout.due)) {
		assert(this == timeout.prev->timeout.next);
		if(timeout.prev == scheduler.timeouts)
			scheduler.timeouts = this;
		if(timeout.next) {
			assert(this == timeout.next->timeout.prev);
			timeout.next->timeout.prev = timeout.prev;
		}
		timeout.prev->timeout.next = timeout.next;
		timeout.next = timeout.prev;
		timeout.prev = timeout.next->timeout.prev;
		timeout.next->timeout.prev = this;
		if(timeout.prev)
			timeout.prev->timeout.next = this;
	}
}

void Task::unlink_timeout() {
	timeout.due = 0;
	if(this == scheduler.timeouts) {
		assert(!timeout.prev);
		scheduler.timeouts = timeout.next;
	}
	if(timeout.prev) {
		assert(this == timeout.prev->timeout.next);
		timeout.prev->timeout.next = timeout.next;
	}
	if(timeout.next) {
		assert(this == timeout.next->timeout.prev);
		timeout.next->timeout.prev = timeout.prev;
	}
	timeout.next = timeout.prev = NULL;
}

void Task::set_read_timeout(uint32_t millisecs) { // 0 to clear
	set_timeout(timeout.read,timeout.write,millisecs);
}

void Task::set_write_timeout(uint32_t millisecs) {
	set_timeout(timeout.write,timeout.read,millisecs);
}

void Task::set_timeout(Timeout::Data& to,Timeout::Data& other,uint32_t millisecs) {
	if(!scheduler.timeouts_enabled)
		return;
	const bool was_scheduled = to.due;
	to.timeout = millisecs_to_time64(millisecs);
	to.due = (scheduler.get_now() + to.timeout);
	if(!to.due && was_scheduled) {
		if(other.due)
			timeout.due = other.due;
		else {
			timeout.due = 0;
			if(this == scheduler.timeouts) {
				assert(!timeout.prev);
				scheduler.timeouts = timeout.next;
			}
			if(timeout.prev) {
				assert(this == timeout.prev->timeout.next);
				timeout.prev->timeout.next = timeout.next;
			}
			if(timeout.next) {
				assert(this == timeout.next->timeout.prev);
				timeout.next->timeout.prev = timeout.prev;
			}
			timeout.next = timeout.prev = NULL;
		}
	} else if(to.due) {
		if(other.due)
			timeout.due = std::min(other.due,to.due);
		else {
			timeout.due = to.due;
			if(!was_scheduled) {
				assert(!timeout.prev && !timeout.next);
				timeout.next = scheduler.timeouts;
				if(timeout.next) {
					assert(!timeout.next->timeout.prev);
					timeout.next->timeout.prev = this;
				}
				scheduler.timeouts = this;
			}
		}
	}
}

void Task::handle_timeout(const time64_t& now) {
	// default behaviour, overriden by subclasses if appropriate, is just to write some debug; the scheduler always closes timed-out task
	if(Log(LOG_CONN)) {
		dump_context(stdout);
		printf("timeout");
		if(timeout.read.due && (now >= timeout.read.due))
			printf(" read (%"PRIu32")",time64_to_millisecs(timeout.read.timeout));
		if(timeout.write.due && (now >= timeout.write.due))
			printf(" write (%"PRIu32")",time64_to_millisecs(timeout.write.timeout));
		putchar('\n');
	}
}

bool Task::is_closed() const {
	return (closed || half_close);
}

void Task::set_nonblocking() {
	set_nonblocking(fd);
}

void Task::set_nonblocking(int fd) {
	int old_flags = fcntl(fd,F_GETFL,0);
	check(old_flags);
	check(fcntl(fd,F_SETFL,old_flags|O_NONBLOCK));
}

void Task::set_nodelay(bool enabled) {
	set_nodelay(fd,enabled);
}

void Task::set_nodelay(int fd,bool enabled) {
	int flag = enabled;
	check(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(char*)&flag,sizeof(int)));
}

void Task::set_cloexec() {
	set_cloexec(fd);
}

void Task::set_cloexec(FD fd) {
	int old_flags = fcntl(fd,F_GETFL,0);
	check(old_flags);
	check(fcntl(fd,F_SETFL,old_flags|FD_CLOEXEC));
}

void Task::schedule(uint32_t flags) {
	const bool added = event.events;
	event.events |= flags;
	check(epoll_ctl(scheduler.getfd(),added?EPOLL_CTL_MOD:EPOLL_CTL_ADD,fd,&event));
}

void Task::unschedule(uint32_t flags) {
	if(event.events) {
		event.events &= ~flags;
		const bool remove = !(~EPOLLET & event.events);
		check(epoll_ctl(scheduler.getfd(),remove?EPOLL_CTL_DEL:EPOLL_CTL_MOD,fd,&event));
		if(remove)
			event.events = 0;
	}
}

bool Task::async_read(void* ptr,ssize_t bytes,ssize_t& read) {
	if(is_closed())
		ThrowInternalError("cannot read when closed");
	if(sated)
		ThrowInternalError("shouldn\'t read when sated");
	read = 0;
	assert(0<bytes);
	uint8_t* c = reinterpret_cast<uint8_t*>(ptr);
	while(read < bytes) {
		if(read_ahead_ofs < read_ahead_len) {
			const ssize_t buffered = std::min<ssize_t>(bytes-read,read_ahead_len-read_ahead_ofs);
			memcpy(c+read,read_ahead_buffer+read_ahead_ofs,buffered);
			read_ahead_ofs += buffered;
			if(read_ahead_ofs == read_ahead_len)
				read_ahead_ofs = read_ahead_len = 0;
			read += buffered;
		} else {
			const bool buffer = (read_ahead_buffer && (ptr != read_ahead_buffer) && ((bytes-read) < read_ahead_maxlen));
			const ssize_t read_ret = ::read(fd,
				buffer? read_ahead_buffer+read_ahead_len: c+read,
				buffer? read_ahead_maxlen-read_ahead_len: bytes-read);
			if(0>read_ret) {
				if(EWOULDBLOCK==errno) {
					sated = true;
					return false;
				}
				fail("async_read()");
			} else if(!read_ret) {
				eoinput = true;
				sated = true;
				if(RUNNING_ON_VALGRIND) {
					dump_context(stdout);
					printf("async_read(ptr,%d,%zu) end of input stream\n",(int)bytes,read);
				}
				ThrowEndOfStreamError();
			} else {
				totalRead += read_ret;
				if(buffer)
					read_ahead_len += read_ret;
				else
					read += read_ret;							
			}
		}
	}
	assert(read == bytes);
	return true;
}

uint16_t Task::async_read_buffered(uint8_t*& ptr,uint16_t max) {
	if(!read_ahead_buffer)
		ThrowInternalError("cannot read from buffer");
	if(read_ahead_ofs == read_ahead_len) {
		assert(!read_ahead_len);
		if(sated)
			return 0;
		ssize_t read;
		async_read(read_ahead_buffer,read_ahead_maxlen,read);
		read_ahead_len += read;
	}
	if(read_ahead_ofs < read_ahead_len) {
		const uint16_t len = std::min<uint16_t>(max,read_ahead_len-read_ahead_ofs);
		ptr = read_ahead_buffer + read_ahead_ofs;
		read_ahead_ofs += len;
		if(read_ahead_ofs == read_ahead_len)
			read_ahead_ofs = read_ahead_len = 0;
		return len;
	}
	return 0;
}

bool Task::async_read(void* ptr,ssize_t bytes) {
	assert(bytes <= MAX_BUF);
	for(;;) {
		if(buflen == bytes) {
			memcpy(ptr,buf,bytes);
			buflen = 0;
			return true;
		}
		ssize_t read;
		const bool complete = async_read(buf+buflen,bytes-buflen,read);
		buflen += read;
		if(!complete)
			return false;
	}
}

bool Task::async_read(ResizeableBuffer& in,ssize_t& read,ssize_t max) {
	read = 0;
	for(;;) {
		const ssize_t remaining = max? max-read: 512; 
		if(!remaining)
			return false;
		in.ensure_capacity(remaining);
		ssize_t bytes;
		const bool sated = !async_read(in.data(in.length()),remaining,bytes);
		read += bytes;
		in.set_length(in.length() + bytes);
		assert(sated == (bytes < remaining));
		if(sated)
			return true;
	}
}

bool Task::async_read_str(char* s,size_t& len,size_t max) {
	while(len < max) {
		if(!async_read(s+len,1)) {
			s[len] = 0;
			return false;
		}
		if(!s[len] || ('\n'==s[len]))
			break;
		len++;
	}
	s[len+1] = 0;
	return true;
}

void Task::async_write(const void* ptr,size_t len) {
	if(!out) {
		OutConst o(ptr,len);
		if(!o.async_write(this)) {
			out = new OutConst(o);
			schedule(EPOLLOUT);
		}
	} else {
		Out* tail = out;
		while(tail->next)
			tail = tail->next;
		tail->next = new OutConst(ptr,len);
	}
}

void Task::async_write_cpy(const void* ptr,size_t len) {
	/* if ptr cannot be completely written synchronously, a copy of the unsent part is made */
	if(!out) {
		OutConst o(ptr,len);
		if(!o.async_write(this)) {
			const size_t remaining = (len-o.ofs);
			void* buf = malloc(remaining);
			if(!buf)
				ThrowInternalError("out of memory");
			memcpy(buf,reinterpret_cast<const uint8_t*>(ptr)+o.ofs,remaining);
			try {
				out = new OutFree(buf,remaining);
			} catch(...) {
				free(buf);
				throw;
			}
			schedule(EPOLLOUT);
		}
	} else {
		Out* tail = out;
		while(tail->next)
			tail = tail->next;
		void* buf = malloc(len);
		if(!buf)
			ThrowInternalError("out of memory");
		memcpy(buf,ptr,len);
		try {
			tail->next = new OutFree(buf,len);
		} catch(...) {
			free(buf);
			throw;
		}
	}
}

void Task::async_write(Out* o) {
	Cleanup<Out,CleanupRelease> c(o);
	if(!out) {
		if(!c->async_write(this)) {
			out = c.detach();
			schedule(EPOLLOUT);
		}
	} else {
		Out* tail = out;
		while(tail->next)
			tail = tail->next;
		tail->next = c.detach();
	}
}

void Task::async_write(const char* s) {
	async_write(s,strlen(s));
}

void Task::async_printf(const char* fmt,...) {
	va_list ap;
	va_start(ap,fmt);
	async_vprintf(fmt,ap);
	va_end(ap);
}

void Task::async_vprintf(const char* fmt,va_list ap) {
	char buf[1024];
	int len = vsnprintf(buf,sizeof(buf),fmt,ap);
	if(!len) return;
	check(len);
	if(len == sizeof(buf)) { // overflowed?
		char* s;
		len = vasprintf(&s,fmt,ap);
		check(len);
		async_write(new OutFree(s,len));
	} else 
		async_write_cpy(buf,len);
}

bool Task::async_write(const void* ptr,size_t len,size_t& written) {
	if(closed) // ignore half_closed, so don't use is_closed()
		ThrowInternalError("cannot write when closed");
	written = 0;
	const char* c = reinterpret_cast<const char*>(ptr);
	while(written < len) {
		const int write_ret = ::write(fd,c+written,len-written);
		if(0>write_ret) {
			if(EWOULDBLOCK==errno)
				return false;
			else if(EINTR!=errno)
				fail("async_write()");
		} else if(!write_ret)
			ThrowGracefulClose("end of output stream");
		else {
			written += write_ret;
			totalWritten += write_ret;
		}
	}
	assert(written == len);
	return true;
}

void Task::dump_context(FILE* out) const {
	fprintf(out,"%"PRIxPTR" [%04"PRIu64,(intptr_t)this,tid);
	if(-1 == fd)
		fprintf(out,":closed");
	else if(0 > fd)
		fprintf(out,":%d %s",fd,strerror(fd));
	fprintf(out,"] ");
}

uint64_t Task::nexttid() {
	static uint64_t tids = 0;
	return ++tids;
}

void Task::popen(FD fd[3],const char *const cmd[]) {
	int p[3][2] = {{-1,-1},{-1,-1},{-1,-1}};
	try {
		for(int i=0; i<3; i++)
			check(pipe(p[i]));
		const pid_t pid = fork();
		check(pid);
		if(pid) {
#ifdef CHG_PRIO
			if(0 != setpriority(PRIO_PROCESS,0,1)) {
				fprintf(stderr,"%s: Could not change server priority: %d (%s)\n",*cmd,errno,strerror(errno));
			}
#endif
			// parent
			set_nonblocking(fd[STDIN_FILENO] = p[STDIN_FILENO][1]);
			::close(p[STDIN_FILENO][0]);
			set_nonblocking(fd[STDOUT_FILENO] = p[STDOUT_FILENO][0]);
			::close(p[STDOUT_FILENO][1]);
			set_nonblocking(fd[STDERR_FILENO] = p[STDERR_FILENO][0]);
			::close(p[STDERR_FILENO][1]);
		} else {
			// child
			dup2(p[STDIN_FILENO][0],STDIN_FILENO);
			::close(p[STDIN_FILENO][1]);
			dup2(p[STDOUT_FILENO][1],STDOUT_FILENO);
			::close(p[STDOUT_FILENO][0]);
			dup2(p[STDERR_FILENO][1],STDERR_FILENO);
			::close(p[STDERR_FILENO][0]);
#ifdef CHG_PRIO
			if(0 != setpriority(PRIO_PROCESS,0,0)) {
				fprintf(stderr,"%s: Could not change child priority: %d (%s)\n",*cmd,errno,strerror(errno));
			}
#endif
			sched_yield();
			execv(*cmd,const_cast<char*const*>(cmd));
			perror("Could not launch");
			fprintf(stderr," \"%s\"\n",*cmd);
			_exit(1);
		}
	} catch(...) {
		for(int i=0; i<3; i++) {
			::close(p[i][0]);
			::close(p[i][1]);
		}
		throw;
	}
}

bool starts_with(const char* s,const char* prefix) {
	while(*prefix)
		if(*prefix++ != *s++)
			return false;
	return true;
}

bool ends_with(const char* s,const char* suffix) {
	const int slen = strlen(s), tlen = strlen(suffix);
	return ((slen>tlen) && !strcmp(suffix,s+slen-tlen));
}

