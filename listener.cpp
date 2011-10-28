/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "listener.hpp"

#include <arpa/inet.h>
#include <string.h>

void Listener::create(Scheduler& scheduler,const char* name,short port,Factory factory,int backlog,bool reuse_addr) {
	Listener* self = new Listener(scheduler,name,port,factory,backlog,reuse_addr);
	self->construct();
}

Listener::Listener(Scheduler& scheduler,const char* n,short p,Factory f,int b,bool ra):
	Task(scheduler), name(n), port(p), factory(f), backlog(b), reuse_addr(ra) {}
	
void Listener::do_construct() {
	check(fd = socket(AF_INET,SOCK_STREAM,0));
	if(reuse_addr) {
		int yes = 1;
		check(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)));
	}
	sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	check(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)));
	check(listen(fd,backlog));
	schedule(EPOLLIN); //level-triggered
	printf("%s is listening on port %hu...\n",name,port);
}

void Listener::read() {
	// we don't want any errors causing us to stop listening!
	try {
		do {
			//### would be nice to print the IP of incoming requests that fail
			const FD accept_fd = accept(fd,NULL,0);
			if(0>accept_fd) {
				switch(errno) {
				case EWOULDBLOCK:
				case ECONNABORTED: // level-triggered
					return;
				case EINTR:
					break;
				case ENFILE:
				case EMFILE:
					//### hmm, what to do? policy please
					ThrowInternalError("not enough FDs");
				default:
					fail("error in accept");
				}
			}
			factory(scheduler,accept_fd);
		} while(true);
	} catch(Error* e) {
		e->dump(this);
		e->release();
	} catch(std::exception& e) {
		fprintf(stderr,"std::exception: %s\n",e.what());
	} catch(...) {
		fprintf(stderr,"unexpected exception!\n");
	}
}

void Listener::dump_context(FILE* out) const {
	fprintf(out,"Listener[%s@%hu] ",name,port);
}

