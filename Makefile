CC = g++
CFLAGS = -std=c++17 -w -g -Wall -I/usr/include -fpermissive
PROG = main

SCS = openbciserial.cpp main.cpp

SSRC = $(SCS)
OBJS = $(SSRC:.cpp=.o)

TCSRCS = $(SCS) 
TCOBJS = $(TCSRCS:.cpp=.o)

ifeq ($(shell uname),Darwin)
	LIBS = -lpthread -framework CoreAudio -framework CoreMIDI -framework CoreFoundation 
else
	LIBS = -L/usr/local/lib -I/usr/local/include   -L/usr/lib/x86_64-linux-gnu/ -pthread -lbsd -lstk
endif

all: $(PROG)

test:		$(TOBJS)
	@echo "Building test version (testbox)"
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)


testcase:	$(TCOBJS)
	@echo "Building test cases"
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)


$(PROG):	$(OBJS)
	@echo "Object files are $(OBJS)"
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
    
base:
	$(CC) $(CFLAGS) -o main $(SRCSR) $(LIBS)

.cpp.o:		$(SRCS)		
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG)
	rm -f $(OBJS)


