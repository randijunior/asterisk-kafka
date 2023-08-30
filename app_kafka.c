#ifndef AST_MODULE
#define AST_MODULE "app_event"
#endif
#define AST_MODULE_SELF_SYM __internal_my_module_self

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/app.h"

static int hello_world(struct ast_channel *chan, const char *vargs)
{
    ast_log(LOG_WARNING, "Hello World");
    return 0;
}

int load_module(void)
{
    int res = 0;

    res |= ast_register_application("HelloWorld", hello_world, "todo", "todo");

    return res;
}

int unload_module(void)
{
    int res = 0;

    res |= ast_unregister_application("HelloWorld");

    return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Dialplan Event Relay Applications and Functions",
				.load = load_module,
				.unload = unload_module);