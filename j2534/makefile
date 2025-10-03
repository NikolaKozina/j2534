CFLAGS=`pkg-config --cflags --libs libusb-1.0`
INSTALL_PREFIX=/usr/local
INSTALL_LIBDIR=$(INSTALL_PREFIX)/lib
ifeq ($(shell uname -s),Darwin)
LIBRARY=j2534.dylib
else
LIBRARY=j2534.so
endif

j2534: j2534.o
	gcc -shared j2534.o $(CFLAGS) -o $(LIBRARY)
j2534.o: j2534.c
	gcc -O3 -fPIC -c j2534.c $(CFLAGS)
tags: j2534.c
	ctags --c-kinds=+cl * /usr/include/libusb-1.0/libusb.h
clean:
	rm -f j2534.o $(LIBRARY)
install: j2534
	mkdir -p $(INSTALL_LIBDIR)
	mkdir -p $(INSTALL_PREFIX)/include/
	cp j2534.h $(INSTALL_PREFIX)/include/
	cp j2534.pc /usr/lib/pkgconfig/
	cp $(LIBRARY) $(INSTALL_LIBDIR)/$(LIBRARY)
	ln -sf $(INSTALL_LIBDIR)/$(LIBRARY) $(INSTALL_LIBDIR)/lib$(LIBRARY)
