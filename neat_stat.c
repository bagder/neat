#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "neat_internal.h"
#include "neat_core.h"
#include "neat_stat.h"


/* Traverse the relevant subsystems of NEAT and gather the stats
   then format the stats as a json string to return */
void neat_stats_build_json(struct neat_ctx *mgr, char **json_stats)
{
	json_t *json_root;
    struct neat_flow *flow;
    int flowcount;

	neat_log(NEAT_LOG_DEBUG, "%s", __func__);

    flowcount = 0;
	json_root = json_object();

    LIST_FOREACH(flow, &mgr->flows, next_flow) {
        flowcount++;
	    json_object_set_new( json_root, "remote_host", json_string( flow->name ));
        json_object_set_new( json_root, "sock_type", json_integer( flow->socket->type ));
        json_object_set_new( json_root, "sock_protocol", json_integer( neat_stack_to_protocol(flow->socket->stack) ));
        json_object_set_new( json_root, "port", json_integer( flow->port ));
    }

    json_object_set_new( json_root, "Number of flows", json_integer( flowcount ));


	/* TODO: fetch OS-specificon_object_set_new( json_root, "port", json_integer( flow->port ));
     * stats from the respective stacks used for connecting */

	/* Callers must remember to free the output */
	*json_stats = json_dumps(json_root, 0);

	json_decref(json_root);

	return;
}



