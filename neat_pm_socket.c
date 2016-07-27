#include "neat.h"
#include "neat_internal.h"
#include "neat_pm_socket.h"

#include <stdlib.h>
#include <uv.h>
#include <string.h>
#include <jansson.h>
#include <assert.h>

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
    char *read_buffer;
    size_t buffer_size;
    ssize_t nesting_count;
};

static void
on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    json_t *json;
    json_error_t error;
    struct neat_pm_read_data *data = stream->data;

    assert(nread > 0);
    assert(nread != UV_EOF);

    unsigned int current_nesting = data->nesting_count;
    for (ssize_t i = 0; i < nread; ++i) {
        if (buf->base[i] == '{' || buf->base[i] == '[') {
            current_nesting++;
        } else if (buf->base[i] == '}' || buf->base[i] == ']') {
            current_nesting--;
        }
    }
    // neat_log(NEAT_LOG_DEBUG, "Current nesting: %d", current_nesting);

    // Check if we have read everything in one go
    if (current_nesting == 0 && data->buffer_size == 0) {
        data->read_buffer = buf->base;
        data->buffer_size = nread;
        data->nesting_count = -1; // Don't free data->read_buffer at the end
        goto complete_message;
    }

    data->nesting_count = current_nesting;
    data->read_buffer = realloc(data->read_buffer, data->buffer_size + nread);
    assert(data->read_buffer);
    memcpy(data->read_buffer + data->buffer_size, buf->base, nread);
    data->buffer_size += nread;

    // Check if this is the last part of a JSON message. If it's not, return and
    // wait for the next part.
    if (current_nesting) {
        neat_log(NEAT_LOG_DEBUG, "Received paratial JSON message, %zu + %zu = %zi",
                 data->buffer_size - nread, nread, data->buffer_size);
        free(buf->base);
        return;
    }
complete_message:
    neat_log(NEAT_LOG_DEBUG, "on_read pm, got %d bytes", data->buffer_size);

    json = json_loadb(data->read_buffer, data->buffer_size, 0, &error);
    if (!json) {
        neat_log(NEAT_LOG_DEBUG, "Failed to read JSON reply from PM");
        neat_log(NEAT_LOG_DEBUG, "Error at position %d:", error.position);
        neat_log(NEAT_LOG_DEBUG, error.text);
        goto end;
    }

    free(buf->base);

    data->on_pm_reply(data->ctx, data->flow, json);

end:
    uv_read_stop(stream);
    neat_pm_socket_close(data->ctx, data->flow, stream);
    free(data);
}

static void
on_request_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    neat_log(NEAT_LOG_DEBUG, "on_request_alloc");
    buf->base = malloc(4096);
    buf->len = 4096;
    assert(buf->base);
}

static void
on_written(uv_write_t* wr, int status)
{
    neat_log(NEAT_LOG_DEBUG, "on_written, status %d", status);

    struct neat_pm_read_data *data = wr->data;
    data->flow->pm_context->pm_handle->data = wr->data;
    uv_read_start(data->flow->pm_context->pm_handle, on_request_alloc, on_read);

    free(wr);
}

static void
on_pm_connected(uv_connect_t* req, int status)
{
    struct neat_pm_connect_data *data = req->data;

    NEAT_FUNC_TRACE();

    if (status < 0) {
        neat_log(NEAT_LOG_DEBUG, "Failed to connect to PM socket");

        /* Exit early if the PM is not running in order to prevent stalling the
         * buildbot tests.
         * TODO: Remove once the buildbots are running the PM.
         */
        exit(-1);

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
    free(req);
    free(data);
}

neat_error_code
neat_pm_socket_connect(neat_ctx *ctx, neat_flow *flow, pm_callback cb)
{
    const char *socket_path;
    const char *home_dir;
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
        if ((home_dir = getenv("HOME")) == NULL) {
            neat_log(NEAT_LOG_DEBUG, "Unable to locate the $HOME directory");
            return NEAT_ERROR_INTERNAL;
        }

        if (snprintf(buffer, 128, "%s/.neat/neat_pm_socket", home_dir) < 0) {
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
neat_pm_send(struct neat_ctx *ctx, struct neat_flow *flow, const char *buffer, pm_reply_callback cb)
{
    NEAT_FUNC_TRACE();

    assert(buffer);

    struct neat_pm_read_data *data = calloc(1, sizeof(*data));
    data->ctx = ctx;
    data->flow = flow;
    data->on_pm_reply = cb;

    uv_write_t *req = malloc(sizeof(*req));
    req->data = data;

    uv_buf_t *buf = malloc(sizeof(uv_buf_t));

    buf->base = (char*)buffer;
    buf->len = strlen(buffer);

    uv_write(req, flow->pm_context->pm_handle, buf, 1, on_written);

    return NEAT_OK;
}

neat_error_code
neat_pm_recv(neat_ctx *ctx, neat_flow *flow)
{

    return NEAT_OK;
}
