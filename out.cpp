/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "out.hpp"
#include "task.hpp"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

Out::Out(const void* p,size_t l):  next(NULL), ptr(p), len(l), ofs(0) {}

void Out::dump_debug(FILE* out) const {
	enum { MAX_DUMP = 45 };
	const char* c = reinterpret_cast<const char*>(ptr);
	if(!c)
		fprintf(out,"(null)");
	else {
		fprintf(out,"%zu bytes ",len);
		for(size_t i=0; (i<len)&&(i<MAX_DUMP); i++, c++)
			if(isprint(*c))
				fputc(*c,out);
			else if('\n'==*c)
				fprintf(out,"\\n");
			else if('\r'==*c)
				fprintf(out,"\\r");
			else
				fprintf(out,"\\%o",*c);
		if(MAX_DUMP<=len)
			fprintf(out," ...");
	}
}

bool Out::async_write(Task* task) {
	const char* c = reinterpret_cast<const char*>(ptr);
	size_t written;
	const bool completed = task->do_async_write(c+ofs,len-ofs,written);
	ofs += written;
	if(!completed)
		return false;
	assert(ofs == len);
	return true;
}

OutConst::OutConst(const void* ptr,size_t len): Out(ptr,len) {}

OutConst::OutConst(const OutConst& copy): Out(copy.ptr,copy.len) {
	ofs = copy.ofs;
}

void OutConst::release() {
	delete this;
}

OutFree::OutFree(const void* ptr,size_t len): Out(ptr,len) {}

void OutFree::release() {
	free(const_cast<void*>(ptr));
	delete this;
}

ResizeableBuffer::ResizeableBuffer(void*& p,size_t& l,size_t initial_capacity): ptr(reinterpret_cast<char*&>(p)), len(l), capacity(0) {
	ptr = NULL;
	len = 0;
	resize(initial_capacity);
}

ResizeableBuffer::~ResizeableBuffer() {
	free(ptr);
	ptr = NULL;
	len = 0;
}

ResizeableBuffer& ResizeableBuffer::write(const char* str) {
	return write_ptr(str,strlen(str));
}

ResizeableBuffer& ResizeableBuffer::write_ptr(const void* aptr,size_t alen) {
	if(1>alen)
		ThrowInternalError("invalid length");
	ensure_capacity(alen);
	memcpy(ptr+len,aptr,alen);
	len += alen;
	return *this;
}

ResizeableBuffer& ResizeableBuffer::nprintf(size_t maxlen,const char* const fmt,...) {
	va_list args;
	va_start(args,fmt);
	ensure_capacity(maxlen);
	const int used = vsnprintf(ptr+len,maxlen,fmt,args);
	va_end(args);
	check(used);
	if(used==(int)maxlen)
		ThrowInternalError("buffer overflow");
	len += used;
	return *this;
}

void ResizeableBuffer::ensure_capacity(size_t needed) {
	//### if profiling says we do lots of small reallocs, we can always do doubling
	if(len+needed>capacity)
		resize(len+needed);
}

void ResizeableBuffer::resize(size_t new_capacity) {
	if(!new_capacity) {
		free(ptr);
		ptr = NULL;
		capacity = 0;
	} else if(!ptr) {
		ptr = (char*)malloc(new_capacity);
		if(!ptr)
			ThrowInternalError("out of memory");
		capacity = new_capacity;
	} else if(char* newptr = (char*)realloc(ptr,new_capacity)) {
		ptr = newptr;
		capacity = new_capacity;
	} else
		ThrowInternalError("out of memory");
}

const char* ResizeableBuffer::c_str() {
	write(0,1); // terminator
	len--; // but its not really there
	return ptr;
}

int ResizeableBuffer::find(const char* str,int start) const { 
	const size_t slen = strlen(str);
	assert(len > 0);
	if(start+slen <= len) {
		const char* found = strstr(ptr+start,str);
		if(found)
			return (found - ptr);
	}
	return -1; // not found
}

bool ResizeableBuffer::starts_with(const char* str) const {
	const size_t slen = strlen(str);
	if(len >= slen)
		return !memcmp(ptr,str,slen);
	return false;
}

bool ResizeableBuffer::ends_with(const char* str) const {
	const size_t slen = strlen(str);
	if(len >= slen)
		return !memcmp(ptr+len-slen,str,slen);
	return false;
}

void ResizeableBuffer::reset(size_t max_size) {
	len = 0;
	if(max_size < capacity)
		resize(max_size);
}

void ResizeableBuffer::set_length(size_t explicit_len) {
	ensure_capacity(explicit_len);
	len = explicit_len;
}

const char* BufferReader::ptr() const {
	assert(stop >= start);
	assert(stop <= in.length());
	return in.c_str() + start;
}

void BufferReader::next() {
	start = stop;
	assert(start <= in.length());
}

void BufferReader::skip_whitespace() {
	while((stop < in.length()) && (*reinterpret_cast<const char*>(in.data(stop)) <= ' '))
		stop++;
	next();
}

size_t BufferReader::next(const void* terminator,size_t terminator_len) {
	next();
	if(start >= in.length())
		return 0;
	const char* buf = reinterpret_cast<const char*>(in.data());
	if(!terminator_len) { // special case to look for the \0 character
		for(;buf[stop]; stop++)
			if(stop == in.length()) {
				stop = start;
				return 0;
			}
		return stop-start;
	}
	const char* found = reinterpret_cast<const char*>(memmem(buf+start,in.length()-start,terminator,terminator_len));
	if(!found)
		return 0;
	stop = (found-buf)+terminator_len;
	return (stop-start);
}

size_t BufferReader::next(const char* terminator) {
	return next(terminator,strlen(terminator));
}

Buffer::Buffer(size_t initial_capacity): ResizeableBuffer(Buffer::ptr,Buffer::len,initial_capacity) {}

OutBuffer::OutBuffer(size_t initial_capacity): Out(NULL,0),
    ResizeableBuffer(const_cast<void*&>(Out::ptr),const_cast<size_t&>(Out::len),initial_capacity) {}

void OutBuffer::release() {
	delete this;
}

