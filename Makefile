# Makefile copied from avr-libc's example "demo"
PRG            = dbg_putchar
OBJ            = main.o dbg_putchar.o
MCU_TARGET     = attiny85
OPTIMIZE       = -Os

DEFS           = -DF_CPU=8000000
LIBS           =

# avrdude related
DEVICE     = attiny85
PROGRAMMER = bsd
PORT       = /dev/parport0
BAUD       = 19200

# You should not have to change anything below here.
CC             = avr-gcc

# Override is only needed by avr-lib build system.
override CFLAGS        = -g -Wall $(OPTIMIZE) -mmcu=$(MCU_TARGET) $(DEFS)
override LDFLAGS       = -Wl,-Map,$(PRG).map

OBJCOPY        = avr-objcopy
OBJDUMP        = avr-objdump

all: $(PRG).elf text eeprom prog

$(PRG).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo
	@echo Sizing data...
	avr-size -B -d --target=ihex $(PRG).elf

# dependency:
main.o: dbg_putchar.h
dbg_putchar.o: dbg_putchar.h

clean:
	rm -rf *.o $(PRG).elf *.bak 
	rm -rf *.hex *.map $(EXTRA_CLEAN_FILES)

# Rules for building the .text rom images
text: hex

hex:  $(PRG).hex

%.hex: %.elf
	@echo
	@echo Generating .hex
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

# Rules for building the .eeprom rom images

eeprom: ehex

ehex:  $(PRG)_eeprom.hex

%_eeprom.hex: %.elf
	@echo
	@echo Generating EEPROM .hex
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O ihex $< $@ \
	|| { echo empty $@ not generated; exit 0; }

prog:   
	@echo
	@echo Programming...
	avrdude -q -q -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -U flash:w:$(PRG).hex:i
	@echo
	@echo Setting RESET pin 
	~/perl/parport.pl 5 1
