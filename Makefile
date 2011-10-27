# (c) William Edwards, 2011
# Using the Simplified BSD License.  See LICENSE file for details

CC = gcc
CPP = g++
LD = g++

HYGIENE = -g3 -Wall #-pedantic-errors -std=c++98 -Wno-long-long -fdiagnostics-show-option
DEBUG = -O0
OPTIMISATIONS = #-O9 -fomit-frame-pointer -fno-rtti -march=native # etc -fprofile-generate/-fprofile-use

# default flags
CFLAGS = ${HYGIENE} ${DEBUG} ${OPTIMISATIONS} ${C_EXT_FLAGS}
CPPFLAGS = ${CFLAGS}
LDFLAGS = ${HYGIENE} ${DEBUG} ${OPTIMISATIONS}

#target binary names

TRG_HELLO = helloworld

OBJ_HELLO_CPP = \
	helloworld.opp \
	http.opp \
	task.opp \
	out.opp \
	error.opp \
	time.opp \
	listener.opp \
	console.opp

OBJ_HELLO_C = 
		
OBJ_CPP = ${OBJ_HELLO_CPP}

OBJ_C = ${OBJ_HELLO_C}

OBJ = ${OBJ_CPP}  ${OBJ_C}

TARGETS = ${TRG_HELLO}

all:	${TARGETS}

${TRG_HELLO}:	${OBJ_HELLO_CPP} ${OBJ_HELLO_C}
	${LD} ${CPPFLAGS} -o $@ $^ ${LDFLAGS}

# compile c files
	
%.o:	%.c
	${CC} ${CFLAGS} -c $< -MD -MF $(<:%.c=%.dep) -o $@

# compile c++ files
	
%.opp:	%.cpp
	${CPP} ${CPPFLAGS} -c $< -MD -MF $(<:%.cpp=%.dep) -o $@
#misc

.PHONY:	clean all
clean:
	rm -f ${TARGETS}
	rm -f *.[hc]pp~ Makefile~ core
	rm -f ${OBJ}
	rm -f $(OBJ_C:%.o=%.dep) $(OBJ_CPP:%.opp=%.dep)

-include $(OBJ_C:%.o=%.dep) $(OBJ_CPP:%.opp=%.dep)

