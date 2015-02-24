CFLAGS=`pkg-config --cflags --libs libusb`
INSTALL_PREFIX=/usr/local
INSTALL_LIBDIR=$(INSTALL_PREFIX)/lib/

j2534: j2534.o
	gcc -g -shared j2534.o $(CFLAGS) -o j2534.so
j2534.o: j2534.c
	gcc -g -fPIC -c j2534.c $(CFLAGS)
tags: j2534.c
	ctags --c-kinds=+cl * /usr/include/libusb-1.0/libusb.h
clean:
	rm j2534.o
install:
	mkdir -p $(INSTALL_LIBDIR)
	mkdir -p $(INSTALL_PREFIX)/include/
	cp j2534.h $(INSTALL_PREFIX)/include/
	cp j2534.pc /usr/lib/pkgconfig/
	cp j2534.so $(INSTALL_LIBDIR)/j2534.so
