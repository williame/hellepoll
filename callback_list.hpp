/* (c) William Edwards, 2011
   Using the Simplified BSD License.  See LICENSE file for details */

#ifndef CALLBACK_LIST_HPP
#define CALLBACK_LIST_HPP

#include "error.hpp"

#include <vector>
#include <algorithm>
#include <assert.h>
#include <cstdio>

template<typename C> class CallbackList {
// a re-entrant safe list of callbacks
public:
	CallbackList(): state(UNLOCKED) {}
	void add(C* c) {
		const size_t count = callbacks.size();
		for(size_t i=0; i<count; i++)
			if(callbacks[i] == c) {
				assert(!"adding duplicate callback");
				return;
			}
		callbacks.push_back(c);
	}
	void remove(C* c) {
		const size_t count = callbacks.size();
		for(size_t i=0; i<count; i++) {
			if(callbacks[i] == c) {
				if(UNLOCKED==state) {
					callbacks[i] = callbacks[count-1];
					callbacks.resize(count-1);
				} else {
					callbacks[i] = NULL;
					state = COMPRESS;
				}
				return;
			}
		}
	}
	size_t count() const { return callbacks.size(); }
	template<typename Func> void notify(Func& f) {
		if(size_t count = callbacks.size()) {
			state = LOCKED;
			if(1 == count) {
				invoke(f,callbacks[0]);
			} else {
				for(size_t i=0; i<count; i++)
					invoke(f,callbacks[i]);
			}
			if(COMPRESS == state) {
				count = callbacks.size();
				Callbacks fresh;
				for(size_t i=0; i<count; i++)
					if(callbacks[i])
						fresh.push_back(callbacks[i]);
				callbacks.clear();
				count = fresh.size();
				for(size_t i=0; i<count; i++)
					callbacks.push_back(fresh[i]);
			}
			state = UNLOCKED;
		}	
	}
private:
	template<typename Func> void invoke(Func& f,C* c) {
		if(!c || c->is_closed())
			return;
		try {
			f(c);
			return;
		} catch(std::exception& e) {
			c->dump_context(stderr);
			fprintf(stderr,"%s: unexpected exception in callback\n",e.what());
		} catch(...) {
			c->dump_context(stderr);
			fprintf(stderr,"unexpected exception in callback\n");
		}
		c->close();
		remove(c);
	}
private:
        typedef std::vector<C*> Callbacks;
        Callbacks callbacks;
        enum {
        	UNLOCKED,
        	LOCKED,
        	COMPRESS
        } state;
};

#endif //CALLBACK_LIST_HPP

