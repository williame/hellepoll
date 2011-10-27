/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef ERROR_HPP
#define ERROR_HPP

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#undef __STDC_FORMAT_MACROS
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <exception>


// poor immitation of shared_ptr


template<typename T> void CleanupDelete(T* t) { delete t; }
template<typename T> void CleanupDeleteArray(T* t) { delete[] t; }
template<typename T> void CleanupFree(T* t) { free(t); }
template<typename T> void CleanupClose(T* t) { t->close(); }
template<typename T> void CleanupRelease(T* t) { t->release(); }

template<typename T,void (Fnc)(T*) = CleanupDelete> class Cleanup {
public:
	Cleanup(): t(NULL) {}
	explicit Cleanup(T* o): t(o) {}
	~Cleanup() { discard(); }
	void attach(T* o) { t = o; }
	T* detach() { T* o = t; t = NULL; return o; }
	void discard() { if(t) Fnc(t); t = NULL; }
	T& operator*() { return *t; }
	T* operator->() { return t; }
	operator bool() { return t; }
	void operator=(T* o) { t = o; }
	T* ptr() { return t; }
	const T* ptr() const { return t; }
private:
	explicit Cleanup(const Cleanup<T,Fnc>& o); // illegal; use = other.detach(); instead
private:
	T* t;
};

class Closeable {
public:
	virtual bool is_closed() const = 0;
	virtual void close() = 0;
};

class ErrorContext {
public:
	virtual void dump_context(FILE* out=stdout) const = 0;
};

class Error {
public:
	virtual ~Error() {}
	virtual void release() { delete this; }
	virtual void dump(const ErrorContext* context = NULL,FILE* out = stderr) = 0;
protected:
	void dump_context(const ErrorContext* context,FILE* out);
};

class EndOfStreamError: public Error {};

/* a HalfClose is an error where any already-queued Out messages are delivered before the connection is closed */
struct HalfClose: public Error {
	const char* msg;
};

void ThrowClientError(const char* fmt,...);
void ThrowInternalError(const char* fmt,...);
void ThrowCError(const char* msg,const char* file,int line);
void ThrowGracefulClose(const char* msg);
void ThrowEndOfStreamError();

#define fail(msg) ThrowCError(msg,__FILE__,__LINE__);
#define check(expr) if(0>(expr)) fail(#expr);

struct Shutdown {
	const char* msg;
};

void ThrowShutdown(const char* msg);

enum LogLevel {
	LOG_CRITICAL =	0x0001,
	LOG_CONN =	0x0002,
	LOG_DEBUG = 	0x0004
};
void InitLog(const char* fname = NULL);
bool Log(LogLevel level);
void SetLog(LogLevel level,bool enable);

#endif //ERROR_HPP

