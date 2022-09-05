utils.o: utils.c utils.h
		gcc -c $<

state.o: state.c utils.h
		gcc -c $<

%: %.c utils.o state.o
		gcc $< utils.o state.o -o $@ -luv -luring

clean:
		rm -f *.o *.x