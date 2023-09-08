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

AST_MUTEX_DEFINE_STATIC(rk_lock);

static rd_kafka_t *rk;

static pthread_t rk_p_thread = AST_PTHREADT_NULL;

static volatile sig_atomic_t run_pooling = 1;

static void *producer_polling_thread(void *data) {
    ast_log(LOG_DEBUG, "Producer polling thread started...");
    while (run_pooling) {
        int events = rd_kafka_poll(rk, 1000);

        if (events == 0 && !run_pooling) {
            break;
        } else {
            ast_log(LOG_DEBUG, "Received %d events\n", events);
        }
    }
    ast_log(LOG_DEBUG, "Producer polling thread stoped...");
    return NULL;
}

static void msg_dr_cb(rd_kafka_t *rkproducer, const rd_kafka_message_t *rkmessage, void *opaque) {
    if (rkmessage->err) {
        ast_log(LOG_ERROR, "(RdKafka) Failed to delivery message: %s\n",
                rd_kafka_err2str(rkmessage->err));
    } else {
        char *msg = (char *)rkmessage->payload;

        ast_log(LOG_DEBUG,
                "(RdKafka) Message delivery sucess (payload: %s)\n"
                "(partition %d" PRId32 ")\n",
                msg, rkmessage->partition);
    }
}

static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    ast_log(LOG_ERROR, "(RdKafka) Error: %s: %s\n", rd_kafka_err2name(err), reason);
}

static void logger(const rd_kafka_t *r, int level, const char *fac, const char *buf) {
    ast_log(LOG_DEBUG, "(RdKafka) %i %s %s\n", level, fac, buf);
}

static rd_kafka_t *create_producer(const char *brokers) {
}

static int rd_kafka_instance_init() {
    rd_kafka_conf_t *conf;
    char *brokers;
    char *level_debug;
    char errstr[512];

    brokers = "localhost:9092";
    level_debug = "msg";

    conf = rd_kafka_conf_new();

    rd_kafka_conf_set_error_cb(conf, error_cb);
    rd_kafka_conf_set_log_cb(conf, logger);
    rd_kafka_conf_set_dr_msg_cb(conf, msg_dr_cb);

    if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "(RdKafka): %s\n", errstr);
        return 1;
    }

    if (rd_kafka_conf_set(conf, "debug", level_debug, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "(RdKafka): %s\n", errstr);
        return 1;
    }

    ast_mutex_lock(&rk_lock);
    rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!rk) {
        rd_kafka_conf_destroy(conf);
        ast_log(LOG_ERROR, "(RdKafka): Failed to create new producer: %s\n",
                errstr);
        return 1;
    }
    ast_mutex_unlock(&rk_lock);

    if (ast_pthread_create_background(&rk_p_thread, NULL, producer_polling_thread, NULL)) {
        ast_log(LOG_ERROR, "Can'not start producer pooling thread");
        rd_kafka_instance_destroy();
        return 1;
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
    ast_log(LOG_DEBUG, "Flushing messages...");

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
                         AST_APP_ARG(msg););

    data = ast_strdupa(vargs);

    if (ast_strlen_zero(data)) {
        ast_log(LOG_WARNING, "%s requires an argument (topic,message)\n", "ProduceToKafka");
        return -1;
    }
    AST_STANDARD_APP_ARGS(args, data);

    ast_log(LOG_NOTICE, "should send message: %s, to topic: %s", args.msg, args.topic);

    if (chan) {
        ast_autoservice_start(chan);
    }
    ast_mutex_lock(&rk_lock);

    err = rd_kafka_producev(
        rk, RD_KAFKA_V_TOPIC(args.topic),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_VALUE(args.msg, strlen(args.msg)),
        RD_KAFKA_V_END);

    if (err) {
        ast_log(LOG_ERROR, "FAILED TO DELIVERY MESSAGE %s\n", rd_kafka_err2str(err));
    } else {
        ast_log(LOG_NOTICE, "Enqueued message: %s\n", args.msg);
    }

    rd_kafka_poll(rk, 0);

    ast_mutex_unlock(&rk_lock);

    if (chan) {
        ast_autoservice_stop(chan);
    }

    return 0;
}

int load_module(void) {
    int res = 0;

    res |= rd_kafka_instance_init();
    res |= ast_register_application("ProduceTo", kafka_producer_exec, "todo", "todo");

    return res;
}

int unload_module(void) {
    int res = 0;

    rd_kafka_instance_destroy();

    res |= ast_unregister_application("ProduceTo");

    return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Kafka Applications and Functions",
                .load = load_module,
                .unload = unload_module);