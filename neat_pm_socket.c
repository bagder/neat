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


struct neat_pm_request_data {
    struct neat_ctx *ctx;
    struct neat_flow *flow;
    pm_reply_callback on_pm_reply;
    char *output_buffer;
    char *read_buffer;
    size_t buffer_size;
    ssize_t nesting_count;
};

static void
on_pm_socket_close(uv_handle_t* handle)
{
    NEAT_FUNC_TRACE();
}

static void
on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    struct neat_pm_request_data *data = stream->data;

    NEAT_FUNC_TRACE();

    // assert(nread > 0);
    // assert(nread != UV_EOF);
    if (nread == UV_EOF) {
        json_t *json;
        json_error_t error;

        neat_log(NEAT_LOG_DEBUG, "Done reading", nread);
        uv_read_stop(stream);
        uv_close((uv_handle_t*)stream, on_pm_socket_close);

        json = json_loadb(data->read_buffer, data->buffer_size, 0, &error);
        if (!json) {
            neat_log(NEAT_LOG_DEBUG, "Failed to read JSON reply from PM");
            neat_log(NEAT_LOG_DEBUG, "Error at position %d:", error.position);
            neat_log(NEAT_LOG_DEBUG, error.text);

            // TODO: Handle this error

            goto end;
        }

        data->on_pm_reply(data->ctx, data->flow, json);
        goto end;
    }

    if (nread < 0) {
        neat_log(NEAT_LOG_DEBUG, "Error");
        goto end;
    }

    neat_log(NEAT_LOG_DEBUG, "Received %d bytes", nread);

    assert(data);

    // data->nesting_count = current_nesting;
    data->read_buffer = realloc(data->read_buffer, data->buffer_size + nread);
    assert(data->read_buffer);
    memcpy(data->read_buffer + data->buffer_size, buf->base, nread);
    data->buffer_size += nread;
end:
    if (buf->base && buf->len)
        free(buf->base);
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
    uv_os_fd_t fd;
    neat_log(NEAT_LOG_DEBUG, "on_written, status %d", status);

    // struct neat_pm_request_data *data = wr->data;
    // data->flow->pm_context->pm_handle->data = wr->data;
    // uv_shutdown_t *req = malloc(sizeof(*req));
    // assert(req);

    uv_read_start(wr->handle, on_request_alloc, on_read);
    wr->handle->data = wr->data;

    // -------------- HACK! -----------------
    uv_fileno((uv_handle_t*)wr->handle, &fd);
    shutdown(fd, SHUT_WR);
    // --------------------------------------

    // free(wr);
}

static void
on_pm_connected(uv_connect_t* req, int status)
{
    struct neat_pm_request_data *data = req->data;

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

    uv_write_t *wr = malloc(sizeof(*wr));
    assert(wr);
    // wr->handle = req->handle;

    uv_buf_t *buf = malloc(sizeof(uv_buf_t));

    buf->base = data->output_buffer;
    buf->len = strlen(buf->base);

    wr->data = data;
    uv_write(wr, req->handle, buf, 1, on_written);

    // data->flow->pm_context->pm_handle = wr->handle;

    free(buf);
error:
    free(req);
    // free(data);
    return;
}

void
neat_pm_socket_close(struct neat_ctx *ctx, struct neat_flow *flow, uv_stream_t *handle)
{
    uv_close((uv_handle_t*)handle, NULL);
}

neat_error_code
neat_pm_send(struct neat_ctx *ctx, struct neat_flow *flow, char *buffer, pm_reply_callback cb)
{
    const char *home_dir;
    // char buffer[128];
    char socket_path_buf[128];
    const char *socket_path;
    struct neat_pm_request_data *data;
    uv_connect_t *connect = malloc(sizeof(uv_connect_t));
    uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));

    NEAT_FUNC_TRACE();

    assert(ctx);
    assert(flow);
    assert(buffer);

    data = malloc(sizeof(*data));
    assert(data);

    data->ctx = ctx;
    data->flow = flow;
    data->output_buffer = buffer;
    data->on_pm_reply = cb;
    data->read_buffer = NULL;
    data->buffer_size = 0;

    connect->data = data;

    // TODO: Move this malloc to neat_flow_init
    // flow->pm_context = malloc(sizeof(struct neat_pm_context));

    uv_pipe_init(ctx->loop, pipe, 1 /* 1 => IPC = TRUE */);

    socket_path = getenv("NEAT_PM_SOCKET");
    if (!socket_path) {
        if ((home_dir = getenv("HOME")) == NULL) {
            neat_log(NEAT_LOG_DEBUG, "Unable to locate the $HOME directory");
            return NEAT_ERROR_INTERNAL;
        }

        if (snprintf(socket_path_buf, 128, "%s/.neat/neat_pm_socket", home_dir) < 0) {
            neat_log(NEAT_LOG_DEBUG, "Unable to construct default path to PM socket");
            return NEAT_ERROR_INTERNAL;
        }
    }

    // flow->pm_context->pm_path = strdup(buffer);

    uv_pipe_connect(connect,
                    pipe,
                    socket_path_buf,
                    on_pm_connected);

    return NEAT_OK;
}
