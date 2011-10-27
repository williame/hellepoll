/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef LISTENER_HPP
#define LISTENER_HPP

#include "task.hpp"

class Listener: private Task {
public:
	typedef void (*Factory)(Scheduler& scheduler,FD accept_fd);
	static void create(Scheduler& scheduler,const char* name,short port,Factory factory,int backlog);
private:
	Listener(Scheduler& scheduler,const char* name,short port,Factory factory,int backlog);
	void dump_context(FILE* out) const;
	void do_construct();
	void read();
private:
	const char* const name;
	const short port;
	const Factory factory;
	const int backlog;
};

#endif //LISTENER_HPP

