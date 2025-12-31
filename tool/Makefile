ARMGNU ?= aarch64-linux-gnu

all : md.q

clean :
	rm -f md.q

md.q: md.c
	$(ARMGNU)-gcc -o md.q md.c
