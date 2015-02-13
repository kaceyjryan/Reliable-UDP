CC=gcc
CFLAGS=-I.
DEPS = # header file 
OBJ = sender.o
OBJ2 = receiver.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: sender receiver

sender: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

receiver: $(OBJ2)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -rf *o receiver sender

