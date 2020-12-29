PROGS = kasa
CFLAGS = -Wall
all: $(PROGS)

clean:
	rm -f *.o $(PROGS)
