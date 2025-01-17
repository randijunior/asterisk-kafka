GCC_BIN = 				gcc
ASTERISK_LIB_DIR =		/usr/lib/asterisk
ASTERISK_SRC_DIR =		$(ASTERISK_DIR)
ASTERISK_BIN	=		/usr/sbin/asterisk

ifeq ($(ASTERISK_SRC_DIR),)
ASTERISK_SRC_DIR = /usr/src/asterisk-18.18.1
endif

clean:
	rm -f res_producer.so res_consumer.so


build-producer:
	$(info Using asterisk headers on $(ASTERISK_SRC_DIR))
	$(GCC_BIN) -shared -o ../bin/res_producer.so -fPIC producer.c config.c -lrdkafka -I$(ASTERISK_SRC_DIR)/include -L$(ASTERISK_SRC_DIR)

build-consumer:
	$(info Using asterisk headers on $(ASTERISK_SRC_DIR))
	$(GCC_BIN) -shared -o ../bin/res_consumer.so -fPIC consumer.c config.c -lrdkafka -I$(ASTERISK_SRC_DIR)/include -L$(ASTERISK_SRC_DIR)

install:
	$(ASTERISK_BIN) -rx 'module unload res_producer.so' > /dev/null 2>&1
	$(ASTERISK_BIN) -rx 'module unload res_consumer.so' > /dev/null 2>&1
	cp bin/*.so $(ASTERISK_LIB_DIR)/modules