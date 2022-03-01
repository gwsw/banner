CFLAGS = -Wall -std=c++11 -O2 -g

.cpp.o:
	g++ $(CFLAGS) -c $<

banner: banner.o
	g++ $(CFLAGS) -o banner banner.o

clean:
	rm -f banner *.o 
