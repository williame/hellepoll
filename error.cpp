/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "error.hpp"

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include "valgrind/memcheck.h"

//#define ASSERT_ON_ERROR

void Error::dump_context(const ErrorContext* context,FILE* out) {
	if(context) {
		context->dump_context(out);
		fprintf(out,"<%"PRIxPTR"> ",(intptr_t)context);
	} else
		fprintf(out,"<no context> ");
}

static char ErrorMessageBuf[100];

void ThrowClientError(const char* fmt,...) {
	struct CE: public Error {
		void release() {}
		void dump(const ErrorContext* context,FILE* out) {
			dump_context(context,out);
			fprintf(out,"client error");
			if(*ErrorMessageBuf)
				fprintf(out,": %s",ErrorMessageBuf);
			fputc('\n',out);
		};
	} static client_error;
	va_list ap;
	va_start(ap,fmt);
	*ErrorMessageBuf = 0;
	vsnprintf(ErrorMessageBuf,sizeof(ErrorMessageBuf),fmt,ap);
	va_end(ap);
	VALGRIND_PRINTF_BACKTRACE("Server Client Error: %s\n",ErrorMessageBuf);
	throw &client_error;
}

void ThrowCError(const char* msg,const char* file,int line) {
	struct CE: public Error {
		void release() {}
		void dump(const ErrorContext* context,FILE* out) {
			dump_context(context,out);
			fprintf(out,"c error: %d (%s) %s @ %s:%d\n",err,strerror(err),msg,file,line);
		}
		const char* msg;
		const char* file;
		int line;
		int err;
	} static c_error;
	if(EINTR == errno)
		ThrowShutdown("program interrupted");
	c_error.msg = msg;
	c_error.file = file;
	c_error.line = line;
	c_error.err = errno;
#ifdef ASSERT_ON_ERROR
	assert(!"ThrowCError");
#endif
	VALGRIND_PRINTF_BACKTRACE("Server C Error: %d (%s) %s\n",errno,strerror(errno),msg);
	throw &c_error;
}

void ThrowEndOfStreamError() {
	struct EOS: public virtual EndOfStreamError {
		void release() {}
		void dump(const ErrorContext* context,FILE* out) {
			dump_context(context,out);
			fprintf(out,"end of stream\n");
		}
	} static eos;
	throw &eos;
}

void ThrowInternalError(const char* fmt,...) {
	struct IE: public Error {
		void release() {}
		void dump(const ErrorContext* context,FILE* out) {
			dump_context(context,out);
			fprintf(out,"internal error");
			if(*ErrorMessageBuf)
				fprintf(out,": %s",ErrorMessageBuf);
			fputc('\n',out);
		};
	} static internal_error;
	va_list ap;
	va_start(ap,fmt);
	*ErrorMessageBuf = 0;
	vsnprintf(ErrorMessageBuf,sizeof(ErrorMessageBuf),fmt,ap);
	va_end(ap);
#ifdef ASSERT_ON_ERROR
	assert(!"ThrowInternalError");
#endif
	VALGRIND_PRINTF_BACKTRACE("Server Internal Error: %s\n",ErrorMessageBuf);
	throw &internal_error;
}

void ThrowGracefulClose(const char* msg) {
	struct GracefulClose: public virtual HalfClose {
		void release() {}
		void dump(const ErrorContext* context,FILE* out) {
			if(msg) {
				dump_context(context,out);
				fprintf(out,"%s\n",msg);
			}
		}
	} static graceful_close;
	graceful_close.msg = msg;
	VALGRIND_PRINTF_BACKTRACE("Server Graceful Close: %s\n",graceful_close.msg);
	throw &graceful_close;
}

void ThrowShutdown(const char* msg) {
	static Shutdown shutdown;
	shutdown.msg = msg;
	throw &shutdown;
}

static unsigned LogFlags = (LOG_CRITICAL); //|LOG_CONN|LOG_DEBUG);

bool Log(LogLevel level) { return (LogFlags&level); }

void SetLog(LogLevel level,bool enable) {
	if(enable)
		LogFlags |= level;
	else
		LogFlags &= ~level;
}

void InitLog(const char* fname) {
	int pipe_fd[2];
	check(pipe(pipe_fd));
	
	const pid_t pid = fork();
	check(pid);
	if(!pid) { // our log child
		close(pipe_fd[1]); // Close unused write end
		FILE* logFile = fname? fopen(fname,"a"): NULL;
		if(fname && !logFile)
			fprintf(stderr,"cannot open log file \"%s\": %d (%s)\n",fname,errno,strerror(errno));
		char ch;
		bool timestamp = true;
		while(read(pipe_fd[0],&ch,1) > 0) {
			if(timestamp) {
				char timebuf[64];
				time_t t = time(NULL);
				strftime(timebuf,sizeof(timebuf),"%y%m%d %T",localtime(&t));
				printf("%s ",timebuf);
				if(logFile)
					fprintf(logFile,"%s ",timebuf);
				timestamp = false;
			}
			putchar(ch);
			if(logFile)
				fputc(ch,logFile);
			if('\n'==ch) {
				timestamp = true;
				fflush(stdout);
				if(logFile)
					fflush(logFile);
			}
		}
		putchar('\n');
		close(pipe_fd[0]);
		if(logFile)
			fclose(logFile);
		exit(EXIT_SUCCESS);
	} else { // Server main process
		close(pipe_fd[0]); // Close unused read end
		// redirect stdout and stderr
		dup2(pipe_fd[1],STDOUT_FILENO);  
		dup2(pipe_fd[1],STDERR_FILENO);  
		close(pipe_fd[1]);  
	}
}

