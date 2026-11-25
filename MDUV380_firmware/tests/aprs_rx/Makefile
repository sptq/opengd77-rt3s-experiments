# Host tests for the RX audio decoders (APRS, DTMF, CW, SSTV), the DMR SMS receive path, the APRS parser, net roster, battery estimate and sked timing (no hardware needed).
#   make -C MDUV380_firmware/tests/aprs_rx
APP    := ../../application
CFLAGS := -O2 -Wall -Wno-misleading-indentation -I$(APP)/include

.PHONY: all clean
all: test_decoder test_parse test_tones test_sstv test_text test_sstv_store test_net test_batt test_sked test_dmr_rx_agc test_emcom_distance test_stqc test_dmr_sms
	./test_decoder
	./test_parse
	./test_tones
	./test_sstv
	./test_text
	./test_sstv_store
	./test_net
	./test_batt
	./test_sked
	./test_dmr_rx_agc
	./test_emcom_distance
	./test_stqc
	./test_dmr_sms

test_decoder: test_decoder.c $(APP)/source/functions/aprsRxDecoder.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_parse: test_parse.c $(APP)/source/functions/aprsRxParse.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_tones: test_tones.c $(APP)/source/functions/toneRxDecoders.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_sstv: test_sstv.c $(APP)/source/functions/sstvRx.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_text: test_text.c $(APP)/source/functions/aprsText.c
	$(CC) $(CFLAGS) -o $@ $^

test_sstv_store: test_sstv_store.c $(APP)/source/functions/sstvStore.c
	$(CC) $(CFLAGS) -DPLATFORM_MDUV380 -Istubs -o $@ $^

test_net: test_net.c $(APP)/source/functions/netRoster.c
	$(CC) $(CFLAGS) -o $@ $^

test_batt: test_batt.c $(APP)/source/functions/batteryRuntime.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_sked: test_sked.c $(APP)/source/functions/skedMath.c
	$(CC) $(CFLAGS) -o $@ $^

test_dmr_rx_agc: test_dmr_rx_agc.c $(APP)/source/functions/dmrRxAgc.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $^ -lm

test_emcom_distance: test_emcom_distance.c $(APP)/source/user_interface/emcomDistance.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $^ -lm

test_stqc: test_stqc.c $(APP)/source/functions/toneRxDecoders.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

test_dmr_sms: test_dmr_sms.c $(APP)/source/functions/dmrSmsPdu.c
	$(CC) $(CFLAGS) -DPLATFORM_MDUV380 -o $@ $^

clean:
	rm -f test_stqc test_dmr_sms test_decoder test_parse test_tones test_sstv test_text test_sstv_store test_net test_batt test_sked test_dmr_rx_agc test_emcom_distance
