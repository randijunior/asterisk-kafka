#ifndef AST_MODULE
#define AST_MODULE "app_kafka"
#endif
#define AST_MODULE_SELF_SYM __internal_my_module_self

#include "asterisk.h"


#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"

#include <librdkafka/rdkafka.h>
#include <stdio.h>
#include <string.h>

static void rd_kafka_instance_destroy(void);
static void stop_pooling(void);
static int define_rd_kafka_conf(rd_kafka_conf_t *conf, const char *key, const char *value);
static int load_config(const char *arr[], unsigned int size, rd_kafka_conf_t *conf, const char *type, struct ast_config *cfg);

#define KAFKA_CONF_FILE "kafka.conf"
#define GENERAL_CONF "general"
#define PRODUCER_CONF "producer"
#define TOPIC_CONF "topic"

#define GENERAL_CONF_MAX_LEN 13
#define PRODUCER_CONF_MAX_LEN 8
#define TOPIC_CONF_MAX_LEN 4

enum general {
    BOOTSTRAP_SERVERS,
    CLIENT_ID,
    LOG_CONNECTION_CLOSE,
    SOCKET_TIMEOUT_MS,
    SOCKET_CONNECTION_SETUP_TIMEOUT_MS,


    MESSAGE_MAX_BYTES,
    MESSAGE_COPY_MAX_BYTES,
    RECEIVE_MESSAGE_MAX_BYTES,
    MAX_IN_FIGHT,
    TOPIC_METADATA_PROPAGATION_MAX_MS,
    TOPIC_BLACKLIST,
    DEBUG,
    RECONNECT_BACKOFF_MS,
    RECONNECT_BACKOFF_MAX_MS,
    ALLOW_AUTO_CREATE_TOPIC,
    CLIENT_RACK,
};


enum producer {
    TOPIC,
    ENABLE_IMDEPOTENCE,
    RETRIES,
    
    BATCH_SIZE,
    DELIVERY_REPORT_ONLY_ERROR,
    QUEUE_BUFFERING_MAX_MESSAGES,
    QUEUE_BUFFERING_MAX_KBYTES,
    LINGER_MS
};

enum topic {
    MESSAGE_TIMEOUT_MS,
    PARTITIONER,

    COMPRESSION_TYPE,
    COMPRESSION_LEVEL
};

static const char *general_conf_arr[] = {
    [BOOTSTRAP_SERVERS] = "bootstrap.servers",
    [CLIENT_ID] = "client.id",
    [LOG_CONNECTION_CLOSE] = "log.connection.close",
    [SOCKET_TIMEOUT_MS] = "socket.timeout.ms",
    [SOCKET_CONNECTION_SETUP_TIMEOUT_MS] = "socket.connection.setup.timeout.ms",
    [MESSAGE_MAX_BYTES] = "message.max.bytes",
    [MESSAGE_COPY_MAX_BYTES] = "message.copy.max.bytes",
    [RECEIVE_MESSAGE_MAX_BYTES] = "receive.message.max.bytes",
    [MAX_IN_FIGHT] = "max.in.flight",
    [TOPIC_METADATA_PROPAGATION_MAX_MS] = "topic.metadata.propagation.max.ms",
    [TOPIC_BLACKLIST] = "topic.blacklist",
    [DEBUG] = "debug",
    [RECONNECT_BACKOFF_MS] = "reconnect.backoff.ms",
    [RECONNECT_BACKOFF_MAX_MS] = "reconnect.backoff.max.ms",
    [ALLOW_AUTO_CREATE_TOPIC] = "allow.auto.create.topics",
    [CLIENT_RACK] = "client.rack",
};


static const char *producer_conf_arr[] = {
    [TOPIC] = "topic",
    [ENABLE_IMDEPOTENCE] = "enable.idempotence",
    [RETRIES] = "retries",

    [BATCH_SIZE] = "batch.size",
    [DELIVERY_REPORT_ONLY_ERROR] = "delivery.report.only.error",
    [QUEUE_BUFFERING_MAX_MESSAGES] = "queue.buffering.max.messages",
    [QUEUE_BUFFERING_MAX_KBYTES] = "queue.buffering.max.kbytes",
    [LINGER_MS] = "linger.ms",
};

static const char *topic_conf_arr[] = {
    [MESSAGE_TIMEOUT_MS] = "message.timeout.ms",
    [PARTITIONER] = "partitioner",

    [COMPRESSION_TYPE] = "compression.type",
    [COMPRESSION_LEVEL] = "compression.level",
};


AST_MUTEX_DEFINE_STATIC(rk_lock);

static rd_kafka_t *rk;
static pthread_t rk_p_thread = AST_PTHREADT_NULL;
static volatile sig_atomic_t run_pooling = 1;
static const char* default_topic = NULL;

static void *producer_polling_thread(void *data) {
    ast_debug(3, "Producer polling thread started...\n");
    while (run_pooling) {
        int events = rd_kafka_poll(rk, 1000);

        if (events == 0 && !run_pooling) {
            break;
        }
    }
    ast_debug(3, "Producer polling thread stoped...\n");
    return NULL;
}

static void msg_dr_cb(rd_kafka_t *rkproducer, const rd_kafka_message_t *rkmessage, void *opaque) {
    if (rkmessage->err) {
        ast_log(LOG_ERROR, "Failed to delivery message: %s\n",
                rd_kafka_err2str(rkmessage->err));
    } else {

        ast_debug(3,
                "Message delivery sucess (payload: %.*s)\n"
                "(partition %d" PRId32 ")\n",
                (int)rkmessage->len, (const char *)rkmessage->payload, rkmessage->partition);
    }
}

static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    ast_log(LOG_ERROR, "Error: %s: %s\n", rd_kafka_err2name(err), reason);
}

static void logger(const rd_kafka_t *r, int level, const char *fac, const char *buf) {
    ast_debug(3, "%i %s %s\n", level, fac, buf);
}

static int read_config_file(rd_kafka_conf_t *conf) {
    struct ast_flags config_flags = { 0 };
    struct ast_config *cfg;
    const char *bootstrap_servers;

    cfg = ast_config_load(KAFKA_CONF_FILE, config_flags);

    if(!cfg) {
        ast_log(LOG_ERROR, "Error reading config file: %s\n", KAFKA_CONF_FILE);
        return 1;
    } else if(cfg == CONFIG_STATUS_FILEINVALID) {
        		ast_log(LOG_ERROR, "Config file " KAFKA_CONF_FILE " is in an invalid format. Aborting.\n");
		return 1;
    }

    if (load_config(general_conf_arr, GENERAL_CONF_MAX_LEN, conf, GENERAL_CONF, cfg) ||
        load_config(producer_conf_arr, PRODUCER_CONF_MAX_LEN, conf, PRODUCER_CONF, cfg) ||
        load_config(topic_conf_arr, TOPIC_CONF_MAX_LEN, conf, TOPIC_CONF, cfg)) {
        ast_log(LOG_ERROR, "Error on define config. Aborting.\n");
        return 1;
    }

    ast_debug(3, "All config loaded and configuraded with success!\n");

    return 0;

}

static int rd_kafka_instance_init() {
    rd_kafka_conf_t *conf;
    char errstr[512];

    conf = rd_kafka_conf_new();

    if(read_config_file(conf)) {
        return 1;
    }

    rd_kafka_conf_set_error_cb(conf, error_cb);
    rd_kafka_conf_set_log_cb(conf, logger);
    rd_kafka_conf_set_dr_msg_cb(conf, msg_dr_cb);


    ast_mutex_lock(&rk_lock);
    rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!rk) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "Failed to create new producer: %s\n",
                errstr);
        return 1;
    }
    ast_mutex_unlock(&rk_lock);

    if (ast_pthread_create_background(&rk_p_thread, NULL, producer_polling_thread, NULL)) {
        ast_log(LOG_ERROR, "Cannot start producer pooling thread");
        rd_kafka_instance_destroy();
        return 1;
    }

    return 0;
}

static int define_rd_kafka_conf(rd_kafka_conf_t *conf, const char *key, const char *value) {
    char *level_debug;
    char errstr[512];

    if (rd_kafka_conf_set(conf, key, value, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "%s\n", errstr);
        return 1;
    }

    return 0;
}

static int load_config(const char *arr[], unsigned int size, rd_kafka_conf_t *conf, const char *type, struct ast_config *cfg) {
    for (unsigned int index = 0; index < size; index++) {
        const char *config = NULL;
        if (!(config = ast_variable_retrieve(cfg, type, arr[index]))) {
            continue;
        }
        if(!strcmp(arr[index], producer_conf_arr[TOPIC])) {
            ast_debug(3,"Setting default topic %s\n", config);
            default_topic = config;
            continue;
        }
        ast_debug(3, "Setting config %s with value %s\n", arr[index], config);
        if (define_rd_kafka_conf(conf, arr[index], config)) {
            ast_log(LOG_ERROR, "Error on define conf. Aborting.\n");
            return 1;
        }
    }

    return 0;
}

static void stop_pooling() {
    run_pooling = 0;
    if (rk_p_thread != AST_PTHREADT_NULL) {
        pthread_kill(rk_p_thread, SIGURG);
        pthread_join(rk_p_thread, NULL);

        rk_p_thread = AST_PTHREADT_NULL;
    }
}

static void rd_kafka_instance_destroy() {
    stop_pooling();
    ast_debug(3, "Flushing messages...\n");

    ast_mutex_lock(&rk_lock);

    rd_kafka_flush(rk, 5 * 1000);
    rd_kafka_destroy(rk);

    ast_mutex_unlock(&rk_lock);
}

static int kafka_producer_exec(struct ast_channel *chan, const char *vargs) {
    char *data;
    struct rk_producer *p;
    char errstr[512];
    rd_kafka_resp_err_t err;

    AST_DECLARE_APP_ARGS(args,
                         AST_APP_ARG(topic);
                         AST_APP_ARG(key);
                         AST_APP_ARG(msg););

    data = ast_strdupa(vargs);

    if (ast_strlen_zero(data)) {
        ast_log(LOG_WARNING, "%s requires an argument (topic, key, message)\n", "ProducerSend");
        return -1;
    }
    AST_STANDARD_APP_ARGS(args, data);

    int topic_not_provided = ast_strlen_zero(args.topic) && ast_strlen_zero(default_topic);

    if (topic_not_provided) {
        ast_log(LOG_WARNING, "Not topic provided");
        return -1;
    }

    if (chan) {
        ast_autoservice_start(chan);
    }
    ast_mutex_lock(&rk_lock);

    const char* topic = ast_strlen_zero(args.topic) ? default_topic : args.topic;

    ast_debug(1, "sending message: \"%s\" with key: \"%s\", to topic: \"%s\"", args.msg, args.key, topic);

    err = rd_kafka_producev(
        rk, RD_KAFKA_V_TOPIC(topic),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_KEY(args.key, strlen(args.key)),
        RD_KAFKA_V_VALUE(args.msg, strlen(args.msg)),
        RD_KAFKA_V_END);

    if (err) {
        ast_log(LOG_ERROR, "FAILED TO DELIVERY MESSAGE %s\n", rd_kafka_err2str(err));
    } else {
        ast_debug(3, "Enqueued message: %s\n", args.msg);
    }


    ast_mutex_unlock(&rk_lock);

    if (chan) {
        ast_autoservice_stop(chan);
    }

    return 0;
}

int load_module(void) {
    int res = 0;

    res = res || rd_kafka_instance_init();
    res = res || ast_register_application("ProducerSend", kafka_producer_exec, "todo", "todo");


    return res;
}

int unload_module(void) {
    int res = 0;

    rd_kafka_instance_destroy();

    res |= ast_unregister_application("ProducerSend");

    return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Kafka Applications and Functions",
                .load = load_module,
                .unload = unload_module);