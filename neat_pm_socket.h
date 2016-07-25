#ifndef NEAT_PM_SOCKET_INCLUDE
#define NEAT_PM_SOCKET_INCLUDE

#include "neat.h"
#include "neat_internal.h"
#include <uv.h>
#include <jansson.h>

typedef void (*pm_callback)(struct neat_ctx *ctx, struct neat_flow *flow);
typedef void (*pm_reply_callback)(struct neat_ctx *ctx, struct neat_flow *flow, json_t *json);

struct neat_pm_context {
    uv_pipe_t pm_pipe;
    uv_stream_t *pm_handle;
};

struct neat_pm_request {
    uv_handle_t handle;
};

neat_error_code neat_pm_socket_connect(struct neat_ctx *ctx, struct neat_flow *flow, pm_callback cb);
neat_error_code neat_pm_send(struct neat_ctx *ctx, struct neat_flow *flow, const char *buffer, pm_reply_callback cb);

#endif /* ifndef NEAT_PM_SOCKET_INCLUDE */
