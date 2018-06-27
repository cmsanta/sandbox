TARGET=render-nodes-minimal
CFLAGS=-Wall -I/usr/include/libdrm

all: Makefile $(TARGET)

$(TARGET): main.o utils.o
	gcc $(CFLAGS) -o drm-gbm main.o utils.o -ldrm -lgbm -lEGL -lGLESv2

clean:
	rm -f $(TARGET)
