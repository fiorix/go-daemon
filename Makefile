all: god

god:
	cc god.c -o god -lpthread

clean:
	rm -f god

.PHONY: god
