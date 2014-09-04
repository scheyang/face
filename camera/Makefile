CURRENT_DIR=$(PWD)
CROSS=arm-none-linux-gnueabi-
LINUX_DIR=$(CURRENT_DIR)/../../../linux/kernel-3.0.8

all: test_camera

test_camera: camera_test.c
	$(CROSS)gcc -o test_camera camera_test.c -I$(LINUX_DIR)/include \
	-I$(LINUX_DIR)/arch/arm/include

clean:
	rm -f test_camera

