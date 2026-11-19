# Host tests for the APRS RX decoder and parser (no hardware needed).
#   make -C MDUV380_firmware/tests/aprs_rx
APP    := ../../application
CFLAGS := -O2 -Wall -Wno-misleading-indentation -I$(APP)/include

.PHONY: all clean
all: test_decoder test_parse test_tones
	./test_decoder
	./test_parse
	./test_tones

test_decoder: test_decoder.c $(APP)/source/functions/aprsRxDecoder.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_parse: test_parse.c $(APP)/source/functions/aprsRxParse.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_tones: test_tones.c $(APP)/source/functions/toneRxDecoders.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f test_decoder test_parse test_tones
