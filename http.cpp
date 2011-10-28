/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#include "http.hpp"

extern "C" {
	#include <string.h>
	#include <stdlib.h>
	#include <sys/socket.h>
	#include <ctype.h>
}

/*** HttpServerConnection ***/

HttpServerConnection::HttpServerConnection(Scheduler& scheduler,FD accept_fd):
	Task(scheduler), read_state(LINE), write_state(LINE), count(0) {
	fd = accept_fd;
}

void HttpServerConnection::do_construct() {
	check(fd);
	schedule(EPOLLIN|EPOLLET);
	setReadAheadBufferSize(sizeof(line));
}

void HttpServerConnection::dump_context(FILE* out) const {
	Task::dump_context(out);
	if(uri[0])
		fprintf(out,"[%s] ",uri);
}

void HttpServerConnection::read() {
	while(!is_closed()) {
		switch(read_state) {
		case LINE:
			// get the request line
			try {
				if(!async_read_in(line,sizeof(uri)-1))
					return;
			} catch(EndOfStreamError*) {
				if(!count)
					throw;
				// so we get end-of-stream when keep-alive?  no problem
				return;
			}
			if(!line.ends_with("\r\n",2))
				HttpError::Throw(HttpError::ERequestURITooLong,*this);
			if(!memcmp(line.cstr(),"\r\n",3)) { // empty lines are ok before request line
				line.clear();
				continue;
			}
			count++;
			read_state = HEADER;
			{
				const char* method = strtok(line.cstr()," ");
				strncpy(uri,strtok(NULL," \r"),sizeof(this->uri));
				const char* v = strtok(NULL,"\r");
				if(!memcmp(v,"HTTP/1.1",9))
					version = HTTP_1_1;
				else if(!memcmp(v,"HTTP/1.0",9))
					version = HTTP_1_0;
				else
					version = HTTP_0_9;
				in_encoding_chunked = false;
				in_content_length = -1; // not known
				keep_alive = out_encoding_chunked = (HTTP_1_1 == version);
				on_request(method,uri);
			}
			line.clear();
			break;
		case HEADER:
			if(!async_read_in(line))
				return;
			if(!memcmp("\r\n",line.cstr(),3)) {
				read_state = BODY;
				if(keep_alive && !in_encoding_chunked && (-1 == in_content_length))
					in_content_length = 0; // length isn't specified, yet its keep-alive, so there is no content
				line.clear();
				on_body();
				break;
			}
			if(!line.ends_with("\r\n",2))
				HttpError::Throw(HttpError::ERequestEntityTooLarge,*this);
			{
				const char* header = strtok(line.cstr()," "), *value = strtok(NULL,"\r");
				if(!ends_with(header,":",1))
					HttpError::Throw(HttpError::EBadRequest,*this);
				if((write_state == LINE) && !strcasecmp(header,"connection:") && !strcasecmp(value,"keep-alive"))
					keep_alive = true;
				else if(!strcasecmp(header,"content-length:")) {
					in_content_length = atoi(value);
					if(in_content_length < 0)
						HttpError::Throw(HttpError::EBadRequest,*this);
				} else if(!strcasecmp(header,"transfer-encoding:"))
					in_encoding_chunked = !strcasecmp(value,"chunked");
				on_header(header,value);
			}
			line.clear();
			break;
		case BODY:
			if(in_encoding_chunked) { //RFC2616-s4.4 says this overrides any explicit content-length header
				ThrowInternalError("in encoding chunked not implemented yet");
			} else if(!keep_alive && (-1 == in_content_length)) {
				// read all available
				uint8_t* chunk;
				while(uint16_t len = async_read_buffered(chunk))
					on_data(chunk,len);
			} else if(-1 != in_content_length) {
				// read all available
				while(in_content_length) {
					uint8_t* chunk;
					if(const uint16_t len = async_read_buffered(chunk,in_content_length)) {
						in_content_length -= len;
						on_data(chunk,len);
					} else
						return;
				}
				if(!keep_alive) {
					read_state = FINISHED;
					shutdown(fd,SHUT_RD);
					return;
				}
				read_state = LINE;
			} else
				ThrowInternalError("cannot cope with combination of keep_alive %d, content_length %d and encoding_chunked %d",
					keep_alive,in_content_length,in_encoding_chunked);
			break;
		default:
			ThrowInternalError("unexpected read_state");
		}
	}
}

void HttpServerConnection::writeResponseCode(int code,const char* message) {
	if(write_state != LINE)
		ThrowInternalError("cannot write response code");
	write_state = HEADER;
	async_printf("HTTP/%s %d %s\r\nConnection: %s\r\n%s",(version==HTTP_1_1?"1.1":"1.0"),code,message,
		keep_alive?"keep-alive":"close",out_encoding_chunked?"Transfer-Encoding: chunked\r\n":"");
}

void HttpServerConnection::writeHeader(const char* header,const char* value) {
	if(write_state == LINE)
		writeResponseCode(200,"OK");
	else if(write_state != HEADER) // could keep a chain to write after the body if chunk encoded
		ThrowInternalError("cannot write response code");
	async_printf("%s: %s\r\n",header,value);
}

void HttpServerConnection::finishHeader() {
	if(write_state == LINE || write_state == HEADER) {
		if(write_state == LINE)
			writeResponseCode(200,"OK");
		async_write("\r\n");
		write_state = BODY;
	} else if(write_state != BODY)
		ThrowInternalError("connection not ready for body");
}

void HttpServerConnection::write(const void* ptr,size_t len) {
	if(!len) return;
	finishHeader();
	if(out_encoding_chunked)
		async_printf("%zx\r\n",len);
	async_write(ptr,len);
}

void HttpServerConnection::write(const char* str) {
	write(str,strlen(str));
}

void HttpServerConnection::writef(const char* fmt,...) {
	va_list ap;
	va_start(ap,fmt);
	char buf[1024];
	int len = vsnprintf(buf,sizeof(buf),fmt,ap);
	if(!len) return;
	check(len);
	if(len == sizeof(buf)) { // overflowed?
		char* s;
		len = vasprintf(&s,fmt,ap);
		check(len);
		write(s,len);
	} else 
		write(buf,len);
	va_end(ap);
}

void HttpServerConnection::finish() {
	finishHeader();
	if(out_encoding_chunked) // finish chunk
		async_write("0\r\n\r\n");
	if(keep_alive) {
		write_state = LINE;
		set_nodelay(true); // will flush it, approximately
		set_nodelay(false);
	} else {
		write_state = FINISHED;
		gracefulClose("finished");
	}
}

void HttpServerConnection::gracefulClose(const char* reason) {
	if(out) {
		half_close = reason;
	} else {
		shutdown(fd,SHUT_RD);
		close();
	}
}

/*** HttpError ***/

const char* const HttpError::ENotFound = "404 Not Found";
const char* const HttpError::ERequestURITooLong = "414 Request-URI Too Long";
const char* const HttpError::ERequestEntityTooLarge = "413 Request Entity Too Large";
const char* const HttpError::EMethodNotAllowed = "405 Method Not Allowed";
const char* const HttpError::EPreconditionFailed = "412 Precondition Failed";
const char* const HttpError::EBadRequest = "400 Bad Request";

void HttpError::Throw(const char* msg,HttpServerConnection& client) {
	client.dump_context(stderr);
	client.async_write("HTTP/1.0 ");
	client.async_write(msg);
	client.async_write("\r\nConnection: close\r\n\r\n");
	ThrowGracefulClose(msg);
};

/*** HttpParams ***/

static bool is_valid_url_char(char c) {
	return ((('0'<=c)&&('9'>=c))||
		(('a'<=c)&&('z'>=c))||
		(('A'<=c)&&('Z'>=c))||
		strchr("$-_.+!*'(),",c));
}
static bool is_special_char(char c) {
	return (('&'==c)||('='==c));
}

static bool to_hex(char c,char& ret) {
	if(('0'<=c)&&('9'>=c))
		ret |= (c-'0');
	else if(('a'<=c)&&('f'>=c))
		ret |= ((c-'a')+10);
	else if(('A'<=c)&&('F'>=c))
		ret |= ((c-'A')+10);
	else
		return false;
	return true;
}

HttpParams::HttpParams(char* p): params(p), len(0), n(0) {
	// first, decode any unnecessary encodings
	char* dest = p;
	if(params) {
		for(const char* src = params; *src; src++) {
			if(is_valid_url_char(*src) || is_special_char(*src)) {
				*dest++ = *src;
			} else if('%'==*src) {
				char hex = 0;
				for(int i=0; i<2; i++) {
					hex <<= 4;
					if(!to_hex(*++src,hex))
						goto abort;
				}
				if(is_valid_url_char(hex)) {
					*dest++ = hex;
				} else { // revert it
					*dest++ = '%';
					*dest++ = src[-1];
					*dest++ = *src;
				}
			} else
				goto abort;
		}
	}
	if(!params) {
		static char empty = '\0';
abort:		params=&empty;
		dest=p;
	}
	// put all the counters at the end
	k = v = len = (dest-p);
}

void HttpParams::restore() {
	if(!len)
		return;
	if(k != len) {
		if(params[v]) {
			params[v-1]='=';
			reencode(params+v,n-v-1);
			v--;
		}
		params[v+strlen(value())]='&';
		reencode(params+k,v-k);
	}
	params[len]='\0';
}

void HttpParams::reset() {
	restore();
	k = v = len;
	n = 0;
}

bool HttpParams::next() {
	if(n>=len)
		return false;
	restore();
	k=n;
	for(int i=k; i<len; i++) {
		if('&'==params[i]) {
			// no value
			params[i]='\0';
			v=i; // to the 0
			n=i+1;
			goto done;
		} else if('='==params[i]) {
			// found it
			params[i]='\0';
			v=i+1;
			// delimit it
			for(int j=v; j<len; j++) {
				if('&'==params[j]) {
					params[j]='\0';
					n=j+1;
					goto done;
				}
			}
			n=len;
			goto done;
		}
	}
	v = n = len;
done:	if(!decode(params+k) || !decode(params+v))
		return false;
	return params[k]; // anonymous values illegal?
}

bool HttpParams::decode(char* dest) {
	for(char* src = dest; *src; src++) {
		if('%'==*src) {
			char hex = 0;
			for(int i=0; i<2; i++) {
				hex <<= 4;
				if(!to_hex(*++src,hex))
					return false;
			}
			*dest++ = hex;
		} else {
			// validate
			if(is_valid_url_char(*src))
				*dest++ = *src;
			else
				return false;
		}
	}
	*dest = 0;
	return true;
}

void HttpParams::reencode(char* p,int len) {
	const int slen = strnlen(p,this->len-(params-p));
	if(slen>=len) // nothing to do?
		return;
	char* dest = (p + len - 1); // the last char of the decoded version
	const char* src = (p + slen - 1); // the last char of the encoded version
	for(; src>=p; src--) {
		assert(dest >= p);
		if(is_valid_url_char(*src))
			*dest-- = *src;
		else {
			char byte = *src;
			for(int i=0; i<2; i++) {
				char hex;
				if(10>(byte&0x0f))
					hex = '0'+(byte&0x0f);
				else
					hex = 'A'+(byte&0x0f);
				*dest-- = hex;
				byte >>= 4;
			}
			*dest-- = '%';
		}
	}
	assert(src==dest);
}

const char* HttpParams::key() const { return (params+k); }

const char* HttpParams::value() const { return (params+v); }

void HttpParams::UnitTest(const char* p) {
	printf("params=\"%s\"\n",p);
	char buf[1024] = {0};
	if(p)
		strncpy(buf,p,sizeof(buf));
	HttpParams params(p? buf: NULL);
	int count=0;
	while(params.next() && count<10) {
		count++;
		printf("\t%d: key=\"%s\", value=\"%s\"\n",count,params.key(),params.value());
		printf("\t\tparams=\"%s\"\n",buf);
	}
	params.reset();
	printf("\tbefore: \"%s\"\n\tafter:  \"%s\"\n",p,buf);
	count=0;
	while(params.next() && count<10) {
		count++;
		printf("\t%d: key=\"%s\", value=\"%s\"\n",count,params.key(),params.value());
		printf("\t\tparams=\"%s\"\n",buf);
	}
}

void upper(char* s) { // in-place
	while(*s) {
		*s = toupper(*s);
		s++;
	}
}

void lower(char* s) { // in-place
	while(*s) {
		*s = tolower(*s);
		s++;
	}
}
