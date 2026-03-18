.PHONY: all build flash

all: dfu spi

build:
	cd build && make -j

dfu: build
	-dfu-util --download hackrf_stock_build/hackrf_usb.dfu

spi: build	
	hackrf_spiflash -w build/hackrf_usb.bin
