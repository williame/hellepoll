/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef OUT_HPP
#define OUT_HPP

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "error.hpp"

class Task;

template<typename T> T extract_be(const void* ptr,size_t ofs,size_t len) {
	assert(len <= sizeof(T));
	const uint8_t* s = reinterpret_cast<const uint8_t*>(ptr)+ofs+len-1;
	T t = 0;
	uint8_t* d = reinterpret_cast<uint8_t*>(&t); 
	for(size_t i=0; i<len; i++)
		*d++ = *s--;
	return t;
}

template<typename T> T extract_le(const void* ptr,size_t ofs,size_t len) {
	assert(len <= sizeof(T));
	T t = 0;
	memcpy(&t,reinterpret_cast<const uint8_t*>(ptr)+ofs,len);
	return t;
}

struct Out {
	virtual void release() = 0;
	void dump_debug(FILE* out) const;
	Out* next; // so sue me
	friend class Task;
protected:
	Out(const void* ptr,size_t len);
	virtual ~Out() {}
protected:
	const void* const ptr;
	const size_t len;
	size_t ofs;
private:
	bool async_write(Task* task);
};

class OutConst: public Out {
public:
	OutConst(const void* ptr,size_t len);
	OutConst(const OutConst& copy);
	~OutConst() {} // deleting explicitly allowed for this specific Out-type
	void release();
};

class OutFree: public Out {
public:
	OutFree(const void* ptr,size_t len);
	void release();
private:
	~OutFree() {}
};

class OutRefCnt: public Out {
public:
	OutRefCnt();
	void release();
private:
	~OutRefCnt() {}
};

template<typename T> class OutDelete: public Out {
public:
	OutDelete(const T& ptr,size_t len=sizeof(T)): Out(&ptr,len) {}
	void release() { delete this; }
private:
	~OutDelete() { delete reinterpret_cast<const T*>(ptr); }
};

template<typename T> class OutDeleteArray: public Out {
public:
	OutDeleteArray(const T* ptr,size_t len): Out(ptr,sizeof(T)*len) {}
	void release() { delete this; }
private:
	~OutDeleteArray() { delete[] reinterpret_cast<const T*>(ptr); }
};

class ResizeableBuffer {
	/* a base-class for managing a resizeable buffer; does NOT own the memory it points at */
public:
	ResizeableBuffer& write(const char* str);
	ResizeableBuffer& write_ptr(const void* ptr,size_t len);
	ResizeableBuffer& nprintf(size_t maxlen,const char* const fmt,...);
	template<typename T> ResizeableBuffer& write(const T& t,size_t len);
	template<typename T> ResizeableBuffer& write_be(const T& t,size_t len);
	void ensure_capacity(size_t needed);
	inline const void* data(size_t ofs = 0) const { assert(ofs <= len); return ptr+ofs; }
	inline void* data(size_t ofs = 0) { assert(ofs <= len); return ptr+ofs; }
	inline size_t length() const { return len; }
	void set_length(size_t explicit_len);
	bool starts_with(const char* str) const;
	int find(const char* str,int start=0) const; //-1 if not found
	bool ends_with(const char* str) const;
	void reset(size_t max_size);
	const char* c_str();
protected:
	ResizeableBuffer(void*& ptr,size_t& len,size_t initial_capacity);
	virtual ~ResizeableBuffer();
	void resize(size_t new_capacity);
protected:
	char*& ptr;
	size_t& len;
	size_t capacity;
};

template<typename T> ResizeableBuffer& ResizeableBuffer::write(const T& t,size_t len) {
	assert(len <= sizeof(T));
	write_ptr(&t,len);
	return *this;
}

template<typename T> ResizeableBuffer& ResizeableBuffer::write_be(const T& t,size_t len) {
	assert(len <= sizeof(T));
	const uint8_t* c = reinterpret_cast<const uint8_t*>(&t);
	for(size_t i=0; i<len; i++)
		write<uint8_t>(c[len-i-1],1);
	return *this;
}

class BufferReader {
public:
	BufferReader(ResizeableBuffer& buffer): in(buffer), start(0), stop(0)  {}
	void next();
	void skip_whitespace();
	size_t next(const void* terminator,size_t terminator_len);
	size_t next(const char* terminator);
	inline size_t remaining() const { return in.length() - stop; } 
	const char* ptr() const;
private:
	ResizeableBuffer& in;
	size_t start, stop;
};

class Buffer: public ResizeableBuffer {
public:
	Buffer(size_t initial_capacity);
private:
	void* ptr;
	size_t len;
};

class OutBuffer: public Out, public ResizeableBuffer {
public:
	typedef ::Cleanup<OutBuffer,CleanupRelease> Cleanup;
	OutBuffer(size_t initial_capacity = 0);
	void release();
private:
	~OutBuffer() {}
};

#endif //OUT_HPP

