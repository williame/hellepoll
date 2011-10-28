/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "error.hpp"
#include "listener.hpp"
#include "console.hpp"
#include "http.hpp"

#include <signal.h>
#include <unistd.h>
#include "valgrind/valgrind.h"

/** todo:
	* rate limiting by being level-triggered with counters in Task read and writes or
		at the frame level, and with shuffling the epoll_wait() results
	* sigaction() and daemon
*/

class HelloWorld: public HttpServerConnection {
public:
	static void factory(Scheduler& scheduler,FD accept_fd);
protected:
	HelloWorld(Scheduler& scheduler,FD accept_fd): HttpServerConnection(scheduler,accept_fd), count(0) {}
	void on_body(); 
private:
	int count;
};

void HelloWorld::factory(Scheduler& scheduler,FD accept_fd) {
	Cleanup<HttpServerConnection,CleanupClose> client(new HelloWorld(scheduler,accept_fd));
	client->construct();
	client.detach();
}

void HelloWorld::on_body() {
	count++;
	writeHeader("Content-Length","18");
	write("Hello ");
	writef("World %6d",count);
	finish();
}

int main(int argc,char* argv[]) {
	printf(	"\n"
		"|_  _ || _  _  _ ||   a blazingly-fast async HTTP server written in C++\n"
		"[ )(/,||(/,[_)(_)||   (c) William Edwards, 2011\n"
		"           |          The Simplified BSD License\n"
		"\n");
	int port = 42042;
	bool console = false, timeouts = true, logging = true;
	int opt;
	while((opt = getopt(argc,argv,"p:chzlr")) != -1) {
		switch(opt) {
		case 'p':
			port = atoi(optarg);
			if(port < 1 || port > 0xffff) {
				fprintf(stderr,"port out of bounds\n");
				return 1;
			}
			break;
		case 'c':
			console = true;
			break;
		case 'z':
			timeouts = false;
			break;
		case 'l':
			logging = false;
			break;
		case '?':
			if('p'==optopt)
				fprintf (stderr,"Option -%c requires an argument.\n",optopt);
			else if(32 < optopt)
				fprintf (stderr,"Unknown option `-%c'.\n",optopt);
			else
				fprintf (stderr,"Unknown option character `\\x%x'.\n",optopt);
             		return 1;
             	default:
             		fprintf(stderr,"unknown option %c\n",opt);
             		// fall through
             	case 'h':
			fprintf(stderr,"usage: ./helloworld {-p [port]} {-f [num]} {-c} {-z} {-l}\n"
				"  -c enables a console (so you can type \"quit\" for a clean shutdown in valgrind)\n"
				"  -z disables all timeouts (useful for test scripts or debugging clients)\n"
				"  -l disables logging to file (logging is turned off if running under valgrind)\n"
				"  -r enables rtmp on port+2 (experimental)\n");
			return 0;
		}
	}
	try {
		if(logging && !RUNNING_ON_VALGRIND)
			InitLog("helloworld.log");
		printf("=== Starting HelloWorld ===\n");
		Scheduler scheduler;
		if(!timeouts)
			scheduler.enable_timeouts(false);
		signal(SIGPIPE, SIG_IGN); // Ignoring SIGPIPE for now ??
		signal(SIGCHLD, SIG_IGN);
		if(console)
			Console::create(scheduler);
		Listener::create(scheduler,"HTTP",port,HelloWorld::factory,100,true);
		scheduler.run();
	} catch(Error* e) {
		e->dump();
		e->release();
	} catch(std::exception& e) {
		fprintf(stderr,"%s",e.what());
	} catch(...) {
		fprintf(stderr,"unexpected exception!\n");
	}
	return 0;
}


