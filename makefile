# makefile for scp

CC = g++
CPP = /usr/local/bin/cpp
LIB = ar
ASM = as

# The places to look for include files (in order).
INCL =  -I./include

# LIBS = -L

LIBRARIES = -lpthread -lstdc++fs

LIBINCL =

# Extra Defs

# utility routines
DEL = rm
COPY = cp

# name of main executable to build
PROG = all

# -O works
# -O1 works
# -O2 works
CFLAGS = $(INCL) -D_REENTRANT -c -g3 -O2 -pipe -DLINUX -D_FILE_OFFSET_BITS=64 -fmessage-length=0 -Wno-reorder -fcoroutines -std=c++20

LINKFLAGS =

# command_line.h has no dependencies

# count_values.h has no dependencies
	
# diskfile.h has no dependencies

include/drscp.h : include/string_functions.h
	touch include/drscp.h

# macros.h has no dependencies

include/string_functions.h : include/macros.h include/x_error.h
	touch include/string_functions.h

# x_error.h has no dependencies
	
src/command_line.cpp : include/command_line.h
	touch src/command_line.cpp
	
src/diskfile.cpp : include/diskfile.h include/string_functions.h
	touch src/diskfile.cpp
	
src/drscp.cpp : include/command_line.h include/count_values.h include/diskfile.h include/drscp.h include/macros.h include/string_functions.h
	touch src/drscp.cpp

src/string_functions.cpp : include/macros.h include/string_functions.h
	touch src/string_functions.cpp
	
bin/command_line.o : src/command_line.cpp
	$(CC) $(CFLAGS) -o $@ src/command_line.cpp

bin/diskfile.o : src/diskfile.cpp
	$(CC) $(CFLAGS) -o $@ src/diskfile.cpp

bin/drscp.o : src/drscp.cpp
	$(CC) $(CFLAGS) -o $@ src/drscp.cpp

bin/string_functions.o : src/string_functions.cpp
	$(CC) $(CFLAGS) -o $@ src/string_functions.cpp

bin/drscp : bin/command_line.o bin/diskfile.o bin/drscp.o bin/string_functions.o
	$(CC) $(LINKFLAGS) bin/command_line.o bin/diskfile.o bin/drscp.o bin/string_functions.o $(LIBRARIES) \
	-o bin/drscp
	
drscp : directories bin/drscp

directories: bin

bin:
	mkdir -p bin

# clean everything
clean :
	rm bin/*
	
FORCE:
