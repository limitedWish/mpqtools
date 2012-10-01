CFLAGS = -Wall -Werror

TARGET = blp2bmp mpqx

ALL: $(TARGET)

mpqx: mpqx.c
	$(CC) -L. -lStorm $^ -o $@

clean:
	rm -f $(TARGET)
