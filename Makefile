CC := gcc
CXX := g++
CFLAGS := -g -Wall -Wextra  -O0
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Linux)
	CFLAGS+=-DCORO_ASM
else
	CFLAGS+=-DCORO_UCONTEXT -D_XOPEN_SOURCE
endif

CXXFLAGS := $(CFLAGS) -std=c++11 -g -O0
TEST_OUT := libcoro_test
RM := rm -f

%.o: %.c
	$(CC) $(CFLAGS)  -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS)  -c $< -o $@

LIBCORO_OBJS :=  libcoro.o scheduler.o coro.o


test1:	test1.cpp $(LIBCORO_OBJS)
	$(CXX) $(CXXFLAGS)  test1.cpp $(LIBCORO_OBJS) -g -o test1  -lpthread -ldl
	
test2:	test2.cpp $(LIBCORO_OBJS)
	$(CXX) $(CXXFLAGS)   test2.cpp $(LIBCORO_OBJS) -g -o test2   -lpthread -ldl -lrt -lstdc++ 

clean:
	$(RM) *.o test1 test2
