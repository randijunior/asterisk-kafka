GCC_BIN = 				gcc
CARGO_BIN = 			cargo
CRATE_DIR = 			http
BINDGEN_BIN = 			cbindgen
ASTERISK_DIR = 			/usr/src/asterisk-18.18.1
ASTERISK_MODULE_DIR = 	/usr/lib/asterisk/modules
SHARED_LIB = 			app_kafka.so


clean:
	rm -f app_kafka.so


build:
	$(GCC_BIN) -shared -o $(SHARED_LIB) -fPIC app_kafka.c -lrdkafka -I/usr/src/asterisk-18.18.1/include -L/usr/lib/asterisk -Wl,-rpath=/usr/lib/asterisk


install:
	cp $(SHARED_LIB) $(ASTERISK_MODULE_DIR)