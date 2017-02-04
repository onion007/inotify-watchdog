GCC = g++
AR = ar


inotify: inotify.c libdlist.a
	$(GCC) $< -o $@ -ldlist -ltinyxml -lpthread -I./ -L./
dlist.o: dlist.c
	$(GCC) -O -c $<
libdlist.a: dlist.o
	$(AR) -rsv $@ $<
install: inotify
	@install -vD inotify ../installer/APPLIANCE_ROOT/sbin/inotify
clean:
	rm -rf dlist.o libdlist.a inotify
