CFLAGS = -O0 -Wall -O2 -g -O2
CPPFLAGS = -O3 $(CFLAGS) -Os -std=c++11 

.cpp.o:
	g++ $(CPPFLAGS) -c $<

banner: banner.o plain_font.o
	g++ $(CPPFLAGS) -o banner $^

plain_font.c: plain.font
	xxd -i $< > $@

clean:
	rm -f banner plain.font.c *.o 
