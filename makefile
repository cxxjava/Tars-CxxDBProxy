
#-----------------------------------------------------------------------

APP          := CxxJava
TARGET       := DBProxyServer
CONFIG       := 
STRIP_FLAG   := N
TARS2CPP_FLAG:= 

CFLAGS       += -std=c++11

KERNEL:=$(shell uname)
LIBDIR = linux
ifeq ($(KERNEL),Darwin)
    LIBDIR = osx
endif

INCLUDE      += -I../../thirdparty/CxxJDK/efc \
				-I/usr/local/Cellar/openssl/1.0.2g/include
LIB          += -L../../thirdparty/CxxJDK/lib/${LIBDIR} -lefc64 -leso64 

ifeq ($(KERNEL),Linux)
    LIB      += -ldl
endif

#-----------------------------------------------------------------------

include /usr/local/tars/cpp/makefile/makefile.tars

#-----------------------------------------------------------------------
