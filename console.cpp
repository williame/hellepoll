/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "console.hpp"

extern "C" {
	#include <fcntl.h>
	#include <stdio.h>
	#include <string.h>
}

void Console::create(Scheduler& scheduler) {
	Console* self = new Console(scheduler);
	self->construct();
}

Console::Console(Scheduler& scheduler): Task(scheduler) {}

void Console::do_construct() {
	fd = fcntl(STDIN_FILENO,F_DUPFD,0);
	schedule(EPOLLIN);
}

void Console::read() {
	for(;;) {
		if(!async_read_in(line))
			return;
		if(!strcasecmp("help\n",line.cstr())) {
			printf("Available commands are:\n"
				"\tquit\n");
		} else if(!strcasecmp("quit\n",line.cstr())) {
			ThrowShutdown("<goodbye>");
		} else {
			printf("Unknown command: try \"help\"\n");
		}
		line.clear();
	}
}

void Console::dump_context(FILE* out) const {
	fprintf(out,"Console ");
}

