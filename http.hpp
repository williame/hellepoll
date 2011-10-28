/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef HTTP_HPP
#define HTTP_HPP

#include "task.hpp"

class HttpError;

void upper(char* s); // in-place
void lower(char* s); // in-place

class HttpServerConnection: private Task {
public:
	void dump_context(FILE* out) const;
	using Task::construct;
	using Task::close;
protected:
	friend class HttpError;
	HttpServerConnection(Scheduler& scheduler,FD accept_fd);
	void do_construct();
	void read();
	void gracefulClose(const char* reason="graceful close");
	// callbacks when a request comes in
	virtual void on_request(const char* method,const char* uri) {}
	virtual void on_header(const char* header,const char* value) {}
	virtual void on_body() {}
	virtual void on_data(const void* chunk,size_t len) {}
	// to respond
	void writeResponseCode(int code,const char* message);
	void writeHeader(const char* header,const char* value);
	void write(const void* ptr,size_t len);
	void write(const char* str);
	void writef(const char* fmt,...);
	void finish();
protected:
	enum {
		HTTP_0_9,
		HTTP_1_0,
		HTTP_1_1,
	} version;
	char uri[1024];
	InLine<1024*5> line;
	bool keep_alive;
private:
	inline void finishHeader();
private:
	enum {
		LINE,
		HEADER,
		BODY,
		FINISHED,
	} read_state, write_state;
	bool in_encoding_chunked, out_encoding_chunked;
	int in_content_length; //-1 means not known
	int count;
};

class HttpError: private HalfClose {
public:
	static const char* const ENotFound;
	static const char* const ERequestURITooLong;
	static const char* const ERequestEntityTooLarge;
	static const char* const EMethodNotAllowed;
	static const char* const EPreconditionFailed;
	static const char* const EBadRequest;
	static void Throw(const char* msg,HttpServerConnection& client);
};

class HttpParams {
public:
	HttpParams(char* params);
	const char* key() const;
	const char* value() const;
	void reset();
	bool next();
	static void UnitTest(const char* params);
private:
	bool decode(char* p);
	void reencode(char* p,int len);
	void restore();
private:
	char* params;
	int len;
	int k, v, n;
};

#endif //HTTP_HPP

