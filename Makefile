.PHONY: all build flash

all: flash

build:
	cd build && make -j

flash: build
	-dfu-util --download hackrf_stock_build/hackrf_usb.dfu
	hackrf_spiflash -w build/hackrf_usb.bin
