
default:	build

clean:
	rm -rf Makefile objs

.PHONY:	default clean

build:
	$(MAKE) -f objs/Makefile

install:
	$(MAKE) -f objs/Makefile install

modules:
	$(MAKE) -f objs/Makefile modules

upgrade:
	/root/nginx/nginx-bin/nginx -t

	kill -USR2 `cat /root/nginx/nginx-bin/nginx.pid`
	sleep 1
	test -f /root/nginx/nginx-bin/nginx.pid.oldbin

	kill -QUIT `cat /root/nginx/nginx-bin/nginx.pid.oldbin`

.PHONY:	build install modules upgrade
