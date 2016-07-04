#include "neat.h"
#include "neat_internal.h"
#include "neat_pm_socket.h"

#include <stdlib.h>
#include <uv.h>
#include <string.h>
#include <jansson.h>
#include <assert.h>

#define ENABLE_WRITE

// Uncomment this to read a single reply only from the PM
#define READ_ONCE

#ifdef ENABLE_WRITE
uv_buf_t buf[1];
static const char* test_string = "{}";
#endif

static void neat_pm_socket_close(struct neat_ctx *ctx, struct neat_flow *flow, uv_stream_t *handle);

struct neat_pm_connect_data {
    struct neat_ctx *ctx;
    struct neat_flow *flow;
    pm_callback on_pm_connected;
};


struct neat_pm_read_data {
    struct neat_ctx *ctx;
    struct neat_flow *flow;
    pm_reply_callback on_pm_reply;
};

static void
on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    // TODO: Fragile
    // Assumes that the reply from PM can be read all at once.

    json_t *json;
    json_error_t error;
    neat_log(NEAT_LOG_DEBUG, "on_read pm, got %d bytes", nread);

    json = json_loadb(buf->base, nread, 0, &error);
    if (!json) {
        neat_log(NEAT_LOG_DEBUG, "Failed to read JSON reply from PM");
        neat_log(NEAT_LOG_DEBUG, "Error at position %d:", error.position);
        neat_log(NEAT_LOG_DEBUG, error.text);
        goto error;
    }

    struct neat_pm_read_data *data = stream->data;
    data->on_pm_reply(data->ctx, data->flow, json);

#ifdef READ_ONCE
    uv_read_stop(stream);
    neat_pm_socket_close(data->ctx, data->flow, stream);
    free(data);
#endif

error:
    free(buf->base);
}

static void
on_request_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    neat_log(NEAT_LOG_DEBUG, "on_request_alloc");
    buf->base = malloc(1024);
    buf->len = 1024;
    assert(buf->base);
}

static void
on_written(uv_write_t* wr, int status)
{
    neat_log(NEAT_LOG_DEBUG, "on_written, status %d", status);

    struct neat_pm_read_data *data = wr->data;
    data->flow->pm_context->pm_handle->data = wr->data;
    uv_read_start(data->flow->pm_context->pm_handle, on_request_alloc, on_read);
}

static void
on_pm_connected(uv_connect_t* req, int status)
{
    struct neat_pm_connect_data *data = req->data;

    NEAT_FUNC_TRACE();

    if (status < 0) {
        neat_log(NEAT_LOG_DEBUG, "Failed to connect to PM socket");
        goto error;
    }

    // Set non-blocking
    if (uv_stream_set_blocking(req->handle, 0) < 0) {
        neat_log(NEAT_LOG_DEBUG, "Failed to set PM socket as non-blocking");
        goto error;
    }

    data->flow->pm_context->pm_handle = req->handle;
    data->on_pm_connected(data->ctx, data->flow);

error:
    free(data);
}

neat_error_code
neat_pm_socket_connect(neat_ctx *ctx, neat_flow *flow, pm_callback cb)
{
    const char *socket_path;
    const char *username;
    char buffer[128];

    NEAT_FUNC_TRACE();

    // TODO: Move this malloc to neat_flow_init
    flow->pm_context = malloc(sizeof(struct neat_pm_context));

    uv_connect_t *connect = malloc(sizeof(uv_connect_t));

    uv_pipe_init(ctx->loop, &flow->pm_context->pm_pipe, 1 /* 1 => IPC = TRUE */);

    struct neat_pm_connect_data *data = malloc(sizeof(*data));
    assert(data);

    data->ctx = ctx;
    data->flow = flow;
    data->on_pm_connected = cb;

    connect->data = data;

    socket_path = getenv("NEAT_PM_SOCKET");
    if (!socket_path) {
        if ((username = getenv("USER")) == NULL) {
            neat_log(NEAT_LOG_DEBUG, "Unable to determine username");
            return NEAT_ERROR_INTERNAL;
        }

        if (snprintf(buffer, 128, "/home/%s/.neat/neat_pm_socket", getenv("USER")) < 0) {
            neat_log(NEAT_LOG_DEBUG, "Unable to construct default path to PM socket");
            return NEAT_ERROR_INTERNAL;
        }

        socket_path = buffer;
    }

    // TODO: check that path is < sizeof(sockaddr_un.sun_path) ?

    uv_pipe_connect(connect,
                    &flow->pm_context->pm_pipe,
                    socket_path,
                    on_pm_connected);

    return NEAT_OK;
}

void
neat_pm_socket_close(struct neat_ctx *ctx, struct neat_flow *flow, uv_stream_t *handle)
{
    uv_close((uv_handle_t*)handle, NULL);
}

neat_error_code
neat_pm_send(struct neat_ctx *ctx, struct neat_flow *flow, pm_reply_callback cb)
{
    NEAT_FUNC_TRACE();

    struct neat_pm_read_data *data = malloc(sizeof(*data));
    data->ctx = ctx;
    data->flow = flow;
    data->on_pm_reply = cb;


#ifdef ENABLE_WRITE
    uv_write_t *req = malloc(sizeof(*req));
    req->data = data;
    buf[0].base = (char*)test_string;
    buf[0].len = strlen(test_string);
    uv_write(req, flow->pm_context->pm_handle, buf, 1, on_written);
#else
    flow->pm_context->pm_handle->data = data;
    uv_read_start(flow->pm_context->pm_handle, on_request_alloc, on_read);
#endif

    return NEAT_OK;
}

neat_error_code
neat_pm_recv(neat_ctx *ctx, neat_flow *flow)
{

    return NEAT_OK;
}
