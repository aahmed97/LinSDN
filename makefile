all: a3sdn

a3sdn: a3sdn.c
	gcc a3sdn.c -o a3sdn

tar: a3sdn.c makefile ReportA3.pdf
	tar -cvf submit.tar a3sdn.c  makefile ReportA3.pdf

clean:
	rm -rf *.o
	rm a3sdn
