# append the name of the local application to the targets
TARGETS+=dumpflash_2_1

# local build options
dumpflash_2_1_CC= -O3 -g3 -Wall -c -fno-strict-aliasing  -fno-common -ffixed-r8 -msoft-float \
          -mcpu=arm7tdmi-s -mtune=arm7tdmi-s -march=armv4t \
          -mthumb -mthumb-interwork -mcallee-super-interworking 
dumpflash_2_1_INC= \
	-I ../../src/interface/ROM_2_1 \
	-I ../../src/common \
	-I ../../src/build/registers \
	-I ../../src/compiler/gnuarm \
	-I ../../src
dumpflash_2_1_LD= -nostartfiles -nostdlib -static
dumpflash_2_1_LIBS=../../libraries/LLC_2_1.a

# list of the objects needed to link dumpflash_2_1
dumpflash_2_1_objects= \
	../../build/dumpflash_2_1/obj/boot/Init-RAMROM.o \
	../../build/dumpflash_2_1/obj/common/Uart1.o \
	../../build/dumpflash_2_1/obj/common/Flash.o \
	../../build/dumpflash_2_1/obj/app/dumpflash.o

../../build/dumpflash_2_1/obj/%.o: ../../src/%.s $(register_files)
	mkdir -p $(@D)
	$(CC) -c $(dumpflash_2_1_CC) -o $@ $(dumpflash_2_1_INC) $<

../../build/dumpflash_2_1/obj/%.o: ../../src/%.c $(register_files)
	mkdir -p $(@D)
	$(CC) -c $(dumpflash_2_1_CC) -o $@ $(dumpflash_2_1_INC) $<


../../build/dumpflash_2_1/dumpflash_2_1.elf: $(dumpflash_2_1_objects)
	$(LD) $(dumpflash_2_1_LD) -Map $(@:.elf=.map) -o $@ $+ $(dumpflash_2_1_LIBS) -T ../../scripts/ld/RAMROM.lds

../../build/dumpflash_2_1/image_flash.bin ../../build/dumpflash_2_1/image_ram.bin: ../../build/dumpflash_2_1/dumpflash_2_1.elf
	@$(BUILDIMAGES) -o $(dir $<)image $<

.PHONY: dumpflash_2_1 dumpflash_2_1_clean dumpflash_2_1_install dumpflash_2_1_flash
.SILENT: dumpflash_2_1 dumpflash_2_1_clean dumpflash_2_1_install dumpflash_2_1_flash
dumpflash_2_1: ../../build/dumpflash_2_1/dumpflash_2_1.elf
	echo "... Finished building dumpflash_2_1 ..."

dumpflash_2_1_install: ../../build/dumpflash_2_1/image_ram.bin
	$(LOAD) $(LOAD_FLAGS) $+

dumpflash_2_1_flash: ../../flasher_2_1/image_ram.bin ../../build/dumpflash_2_1/image_flash.bin
	$(LOAD) $(LOAD_FLAGS) $+

dumpflash_2_1_clean:
	rm -rf ../../build/dumpflash_2_1