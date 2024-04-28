CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

mywebserver: main.cpp  ./timer/timer.cpp ./http/http_conn.cpp ./log/log.cpp ./mysql-connection/mysql_connection_pool.cpp  ./webserver/webserver.cpp ./config/config.cpp
	$(CXX) -o mywebserver  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r mywebserver
