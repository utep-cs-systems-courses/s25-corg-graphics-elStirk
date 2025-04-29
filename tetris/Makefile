# Makefile for the Tetris demo (in tetris/ directory)

#--------------------------------------------------
# Makefile configuration
#--------------------------------------------------
CPU      = msp430g2553
CFLAGS   = -mmcu=${CPU} -Os -I../h -I../timerLib
LDFLAGS  = -L../lib -L/opt/ti/msp430_gcc/include

# switch the compiler (for the internal make rules)
CC       = msp430-elf-gcc
AS       = msp430-elf-gcc -mmcu=${CPU} -c

#--------------------------------------------------
# default target
#--------------------------------------------------
all: tetris.elf

#--------------------------------------------------
# link the ELF
#--------------------------------------------------
# Note: wdt_handler.s is reused from msquares directory
tetris.elf: tetris.o wdt_handler.o
	${CC} ${CFLAGS} ${LDFLAGS} -o $@ $^ -lTimer -lLcd

#--------------------------------------------------
# compile C into object
#--------------------------------------------------
tetris.o: tetris.c
	${CC} ${CFLAGS} -c -o $@ tetris.c

#--------------------------------------------------
# assemble WDT handler from msquares
#--------------------------------------------------
wdt_handler.o: ../msquares/wdt_handler.s
	${AS} -o $@ $<

#--------------------------------------------------
# load to device
#--------------------------------------------------
load: tetris.elf
	msp430loader.sh $^
#--------------------------------------------------
# clean up
#--------------------------------------------------
clean:
	rm -f *.o *.elf
