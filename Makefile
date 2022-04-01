all: log.o fdwrapper.o conn.o mgr.o addr.o springsnail 

log.o: log.cpp log.h
	g++ -c log.cpp -o log.o

fdwrapper.o: fdwrapper.cpp fdwrapper.h
	g++ -c fdwrapper.cpp -o fdwrapper.o

conn.o: conn.cpp conn.h
	g++ -c conn.cpp -o conn.o

mgr.o: mgr.cpp mgr.h
	g++ -c mgr.cpp -o mgr.o

addr.o: InetAddress.cpp InetAddress.h
	g++ -c InetAddress.cpp -o addr.o

springsnail: processpool.h main.cpp log.o fdwrapper.o conn.o mgr.o addr.o
	g++ processpool.h log.o fdwrapper.o conn.o mgr.o addr.o main.cpp -o springsnail

server: 
	g++ util.cpp fdwrapper.cpp InetAddress.cpp server.cpp -o server

client:
	g++ util.cpp InetAddress.cpp client.cpp -o client

clean:
	rm *.o springsnail client server
