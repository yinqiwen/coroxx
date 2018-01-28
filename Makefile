CC := gcc
CXX := g++
CFLAGS := -Wall -Wextra -DCORO_UCONTEXT -D_XOPEN_SOURCE -O0
CXXFLAGS := $(CFLAGS) -std=c++11
TEST_OUT := libcoro_test
RM := rm -f

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

LIBCORO_OBJS :=  libcoro.o scheduler.o coro.o


test1:	test1.cpp $(LIBCORO_OBJS)
	$(CXX) $(CXXFLAGS) test1.cpp $(LIBCORO_OBJS) -o test1
	./test1

clean:
	$(RM) *.o test1