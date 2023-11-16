GCC_BIN = 				gcc
ASTERISK_SRC_DIR = 		/usr/src/asterisk-17.5.0
ASTERISK_DIR =			/usr/lib/asterisk
SHARED_LIB = 			app_kafka.so


clean:
	rm -f app_kafka.so


build:
	$(GCC_BIN) -shared -o $(SHARED_LIB) -fPIC app_kafka.c -lrdkafka -I$(ASTERISK_SRC_DIR)/include -L$(ASTERISK_DIR) -Wl,-rpath=$(ASTERISK_DIR)


install:
	cp $(SHARED_LIB) $(ASTERISK_DIR)/modules