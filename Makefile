all: god

god:
	cc god.c -o god -lpthread

clean:
	rm -f god

install: god
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 god $(DESTDIR)/usr/bin

.PHONY: god
