CFLAGS = -Wall -O2 
CPPFLAGS = $(CFLAGS) -std=c++11 

.cpp.o:
	g++ $(CPPFLAGS) -c $<
.c.o:
	gcc $(CFLAGS) -c $<

banner: banner.o plain_font.o
	g++ $(CPPFLAGS) -o banner $^

plain_font.c: plain.font
	xxd -i $< > $@

clean:
	rm -f banner plain_font.c *.o 
