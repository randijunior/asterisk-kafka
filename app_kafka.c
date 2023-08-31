#ifndef AST_MODULE
#define AST_MODULE "app_event"
#endif
#define AST_MODULE_SELF_SYM __internal_my_module_self

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/app.h"

#include <librdkafka/rdkafka.h>


static volatile sig_atomic_t run = 1;

static pthread_t rk_thraed = AST_PTHREADT_NULL;

static void *rk_poll(void *data) {
    rd_kafka_t **rk;
    if (!(rk = ast_threadstorage_get(&producer_instance, sizeof(*rk))))
    {
        ast_log(LOG_ERROR, "Cannot get producer structure\n");
        return -1;
    }
    while (run && rd_kafka_outq_len(*rk) > 0) { rd_kafka_poll(*rk, 100); }
}

static void msg_delivered_cb(rd_kafka_t *rkproducer, const rd_kafka_message_t *rkmessage, void *opaque)
{
    if (rkmessage->err)
    {
        ast_log(LOG_ERROR, "(RdKafka): Failed to delivery message: %s\n", rd_kafka_err2str(rkmessage->err));
    }
    else
    {
        ast_log(LOG_DEBUG, "(RdKafka): Message delivery sucess (payload: %s)\n"
                           "(partition %d" PRId32 ")\n",
                (char *)rkmessage->payload, rkmessage->partition);
    }
}

static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque)
{
    ast_log(LOG_ERROR, "(RdKafka): %s\n", reason);
}

static void logger_cb(const rd_kafka_t *r, int level, const char *fac, const char *buf)
{
    ast_log(LOG_NOTICE, "(RdKafka): %i %s %s\n", level, fac, buf);
}

static int rd_kafka_instance_init(void *data)
{
    rd_kafka_t **rk = data;
    rd_kafka_conf_t *conf;
    char *brokers;
    char errstr[512];

    brokers = "localhost:9092";

    conf = rd_kafka_conf_new();

    rd_kafka_conf_set_log_cb(conf, logger_cb);
    rd_kafka_conf_set_dr_msg_cb(conf, msg_delivered_cb);

    if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
    {
        ast_log(LOG_ERROR, "(RdKafka): %s\n", errstr);
        return -1;
    }

    *rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!rk)
    {
        ast_log(LOG_ERROR, "(RdKafka): Failed to create new producer: %s\n",
                errstr);
        return -1;
    }
    return 0;
}

static void rd_kafka_instance_destroy(void *data)
{
    rd_kafka_t **rk = data;

    rd_kafka_flush(*rk, 10 * 2000);
    rd_kafka_destroy(*rk);
}

static struct rk_producer *create_producer() {}

AST_THREADSTORAGE_CUSTOM(producer_instance, rd_kafka_instance_init, rd_kafka_instance_destroy);

static int kafka_producer_exec(struct ast_channel *chan, const char *vargs)
{
    char *data;
    rd_kafka_t **rk;
    char errstr[512];
    rd_kafka_resp_err_t err;

    if (!(rk = ast_threadstorage_get(&producer_instance, sizeof(*rk))))
    {
        ast_log(LOG_ERROR, "Cannot alocate producer structure\n");
        return -1;
    }

    AST_DECLARE_APP_ARGS(args,
                         AST_APP_ARG(topic);
                         AST_APP_ARG(msg););

    data = ast_strdupa(vargs);

    if (ast_strlen_zero(data))
    {
        ast_log(LOG_WARNING, "%s requires an argument (topic,message)\n", "ProduceToKafka");
        return -1;
    }
    AST_STANDARD_APP_ARGS(args, data);

    ast_log(LOG_NOTICE, "should send message: %s, to topic: %s", args.msg, args.topic);

    if (chan)
    {
        ast_autoservice_start(chan);
    }

    err = rd_kafka_producev(*rk, 
                            RD_KAFKA_V_TOPIC("quickstart-events"), 
                            RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY), 
                            RD_KAFKA_V_VALUE("a", 1), 
                            RD_KAFKA_V_OPAQUE(NULL), 
                            RD_KAFKA_V_END);

    if (err)
    {
        ast_log(LOG_ERROR, "FAILED TO DELIVERY MESSAGE %s\n", rd_kafka_err2str(err));
    }
    else
    {
        ast_log(LOG_WARNING, "Enqueued message");
    }

    rd_kafka_poll(*rk, 0);

    if (chan)
    {
        ast_autoservice_stop(chan);
    }

    return 0;
}

int load_module(void)
{
    int res = 0;

    res |= ast_register_application("ProduceToKafka", kafka_producer_exec, "todo", "todo");


    return res;
}

int unload_module(void)
{
    int res = 0;

    res |= ast_unregister_application("ProduceToKafka");

    run = 0;

    return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Kafka Applications and Functions",
                .load = load_module,
                .unload = unload_module);