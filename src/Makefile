CC = cc
COPTS = 
LIBS = -lm -lc
PROGRAMS = scheduler skycalc

OBJECTS = scheduler.o scheduler_telescope.o scheduler_camera.o socket.o \
         sky_utils.o ecliptic.o scheduler_fits.o scheduler_corrections.o \
	 scheduler_signals.o  scheduler_status.o

.c.o: 
	$(CC) $(COPTS) -c $<

all: $(PROGRAMS) 



scheduler: $(OBJECTS)
	 $(CC) $(COPTS) -o scheduler $(OBJECTS) $(LIBS)

skycalc: skycalc.o
	 $(CC) $(COPTS) -o skycalc skycalc.o $(LIBS)


clean: 
	rm -f $(PROGRAMS) *.o 

install:
	cp $(PROGRAMS) ../bin





