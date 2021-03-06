#include <neat.h>
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

/**********************************************************************

    HTTP-GET client in neat
    * connect to HOST and send GET request
    * write response to stdout

    client_http_get [OPTIONS] HOST
    -u : URI
    -n : number of requests/flows
    -v : log level (0 .. 2)

**********************************************************************/

#ifndef timersub
#define timersub(tvp, uvp, vvp)                                 \
    do {                                                        \
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;          \
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;       \
        if ((vvp)->tv_usec < 0) {                               \
            (vvp)->tv_sec--;                                    \
            (vvp)->tv_usec += 1000000;                          \
        }                                                       \
    } while (0)
#endif

static int          result                  = 0;
static uint32_t     config_rcv_buffer_size  = 1024*1024; // 1MB rcv buffer
static uint32_t     config_max_flows        = 50;
static uint8_t      config_log_level        = 0;
static char         request[512];
static uint32_t     flows_active            = 0;
static const char   *request_tail           = "HTTP/1.0\r\nUser-agent: libneat\r\nConnection: close\r\n\r\n";
static char         *config_property        = "\
{\
    \"transport\": [\
        {\
            \"value\": \"SCTP\",\
            \"precedence\": 1\
        },\
        {\
            \"value\": \"TCP\",\
            \"precedence\": 1\
        }\
    ],\
    \"multihoming\": {\
        \"value\": true,\
        \"precedence\": 1\
    }\
}";

struct stat_flow {
    uint32_t rcv_bytes;
    uint32_t rcv_bytes_last;
    uint32_t rcv_calls;
    struct timeval tv_first;
    struct timeval tv_last;
    struct timeval tv_delta;
    uv_timer_t timer;
};

static neat_error_code on_close(struct neat_flow_operations *opCB);

static neat_error_code
on_error(struct neat_flow_operations *opCB)
{
    fprintf(stderr, "%s\n", __func__);

    result = EXIT_FAILURE;

    neat_close(opCB->ctx, opCB->flow);
    return NEAT_OK;
}

static neat_error_code
on_readable(struct neat_flow_operations *opCB)
{
    // data is available to read
    unsigned char buffer[config_rcv_buffer_size];
    uint32_t bytes_read = 0;
    struct stat_flow *stat = opCB->userData;
    neat_error_code code;
    struct timeval tv_duration;
    double time_elapsed = 0.0;
    char buffer_filesize_human[32];
    char buffer_bandwidth_human[32];
    struct neat_tlv options[1];

    //last_stream = (last_stream + 1) % opCB->flow->stream_count;
    options[0].tag           = NEAT_TAG_TRANSPORT_STACK;
    options[0].type          = NEAT_TYPE_INTEGER;

    //fprintf(stderr, "%s - reading from flow\n", __func__);
    code = neat_read(opCB->ctx, opCB->flow, buffer, config_rcv_buffer_size, &bytes_read, options, 1);
    if (code == NEAT_ERROR_WOULD_BLOCK) {
        if (config_log_level >= 1) {
            fprintf(stderr, "%s - would block\n", __func__);
        }
        return NEAT_OK;
    } else if (code != NEAT_OK) {
        return on_error(opCB);
    }

    if (!bytes_read) { // eof
        uv_timer_stop(&(stat->timer));

        if (config_log_level >= 1) {
            fprintf(stderr, "%s - neat_read() returned 0 bytes - connection closed\n", __func__);
        }

        timersub(&(stat->tv_last), &(stat->tv_first), &tv_duration);
        time_elapsed = tv_duration.tv_sec + (double)tv_duration.tv_usec / 1000000.0;
        filesize_human(8 * (stat->rcv_bytes) / time_elapsed, buffer_bandwidth_human, sizeof(buffer_bandwidth_human));
        filesize_human(stat->rcv_bytes, buffer_filesize_human, sizeof(buffer_filesize_human));

        printf("########################################################\n");
        printf("# transfer finished! :)\n");
        printf("########################################################\n");
        printf("# size:\t\t%s\n", buffer_filesize_human);
        printf("# duration:\t%.2f s\n", time_elapsed);
        printf("# bandwidth:\t%sit/s\n", buffer_bandwidth_human);
        printf("# protocol:\t");

        switch ((int)options[0].value.integer) {
            case NEAT_STACK_TCP:
                printf("TCP");
                break;
            case NEAT_STACK_SCTP:
                printf("SCTP");
                break;
            case NEAT_STACK_SCTP_UDP:
                printf("SCTP/UDP");
                break;
            default:
                printf("OTHER");
                break;
        }
        printf("\n");

        printf("########################################################\n");

        fflush(stdout);
        on_close(opCB);

    } else if (bytes_read > 0) {
        stat = opCB->userData;
        stat->rcv_bytes += bytes_read;
        stat->rcv_calls++;
        gettimeofday(&(stat->tv_last), NULL);
        if (config_log_level >= 1) {
            fprintf(stderr, "%s - received %d bytes\n", __func__, bytes_read);
            fwrite(buffer, sizeof(char), bytes_read, stdout);
        }
    }
    return NEAT_OK;
}

static neat_error_code
on_writable(struct neat_flow_operations *opCB)
{
    neat_error_code code;
    fprintf(stderr, "%s - sending request\n", __func__);
    code = neat_write(opCB->ctx, opCB->flow, (const unsigned char *)request, strlen(request), NULL, 0);
    if (code != NEAT_OK) {
        return on_error(opCB);
    }
    opCB->on_writable = NULL;
    neat_set_operations(opCB->ctx, opCB->flow, opCB);

    return NEAT_OK;
}

static void
print_timer_stats(uv_timer_t *handle)
{
    struct stat_flow *stat = handle->data;
    struct timeval tv_now, tv_delta;
    double time_elapsed = 0.0;
    char buffer_filesize_human[32];

    gettimeofday(&tv_now, NULL);
    timersub(&tv_now, &(stat->tv_delta), &tv_delta);
    time_elapsed = tv_delta.tv_sec + (double)tv_delta.tv_usec / 1000000.0;
    filesize_human(8 * (stat->rcv_bytes - stat->rcv_bytes_last) / time_elapsed, buffer_filesize_human, sizeof(buffer_filesize_human));

    fprintf(stderr, "%d bytes in %.2fs = %sit/s\n", stat->rcv_bytes - stat->rcv_bytes_last, time_elapsed, buffer_filesize_human);

    stat->rcv_bytes_last = stat->rcv_bytes;
    gettimeofday(&(stat->tv_delta), NULL);
    uv_timer_again(&(stat->timer));
}

static neat_error_code
on_connected(struct neat_flow_operations *opCB)
{
    struct stat_flow *stat = opCB->userData;
    uv_loop_t *loop = neat_get_event_loop(opCB->ctx);
    // now we can start writing
    fprintf(stderr, "%s - connection established\n", __func__);

    gettimeofday(&(stat->tv_first), NULL);
    gettimeofday(&(stat->tv_last), NULL);

    uv_timer_init(loop, &(stat->timer));
    stat->timer.data = stat;
    uv_timer_start(&(stat->timer), print_timer_stats, 0, 1000);

    opCB->on_readable = on_readable;
    opCB->on_writable = on_writable;
    neat_set_operations(opCB->ctx, opCB->flow, opCB);

    return NEAT_OK;
}

static neat_error_code
on_close(struct neat_flow_operations *opCB)
{
    //struct stat_flow *stat = opCB->userData;
    //fprintf(stderr, "%s - flow closed OK - bytes: %d - calls: %d\n", __func__, stat->rcv_bytes, stat->rcv_calls);
	//uv_close((uv_handle_t*)&(stat->timer), NULL);
	//free(stat);

	// cleanup
    opCB->on_close = NULL;
    opCB->on_readable = NULL;
    opCB->on_writable = NULL;
    opCB->on_error = NULL;
    neat_set_operations(opCB->ctx, opCB->flow, opCB);

    // stop event loop if all flows are closed
    flows_active--;
    fprintf(stderr, "%s - active flows left : %d\n", __func__, flows_active);
    if (flows_active == 0) {
        fprintf(stderr, "%s - stopping event loop\n", __func__);
        neat_stop_event_loop(opCB->ctx);
    }

    return NEAT_OK;
}

int
main(int argc, char *argv[])
{
    struct neat_ctx *ctx = NULL;
    struct neat_flow *flows[config_max_flows];
    struct neat_flow_operations ops[config_max_flows];
    int arg = 0;
    uint32_t num_flows = 1; //xxx todo : check for multiple flow
    uint32_t i = 0;
    char *arg_property = NULL;
    result = EXIT_SUCCESS;

    memset(&ops, 0, sizeof(ops));
    memset(flows, 0, sizeof(flows));

    snprintf(request, sizeof(request), "GET %s %s", "/", request_tail);

    while ((arg = getopt(argc, argv, "P:u:n:v:")) != -1) {
        switch(arg) {
        case 'P':
            if (read_file(optarg, &arg_property) < 0) {
                fprintf(stderr, "Unable to read properties from %s: %s",
                        optarg, strerror(errno));
                result = EXIT_FAILURE;
                goto cleanup;
            }
            if (config_log_level >= 1) {
                fprintf(stderr, "%s - option - properties: %s\n", __func__, arg_property);
            }
            break;
        case 'u':
            snprintf(request, sizeof(request), "GET %s %s", optarg, request_tail);
            break;
        case 'n':
            num_flows = strtoul(optarg, NULL, 0);
            if (num_flows > config_max_flows) {
                num_flows = config_max_flows;
            }
            fprintf(stderr, "%s - option - number of flows: %d\n", __func__, num_flows);
            break;
        case 'v':
            config_log_level = atoi(optarg);
            if (config_log_level >= 1) {
                fprintf(stderr, "%s - option - log level: %d\n", __func__, config_log_level);
            }
            break;
        default:
            fprintf(stderr, "usage: client_http_get [OPTIONS] HOST\n");
            goto cleanup;
            break;
        }
    }

    if (optind + 1 != argc) {
        fprintf(stderr, "usage: client_http_get [OPTIONS] HOST\n");
        goto cleanup;
    }

    printf("%d flows - requesting: %s\n", num_flows, request);

    if ((ctx = neat_init_ctx()) == NULL) {
        fprintf(stderr, "could not initialize context\n");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    neat_log_level(ctx, NEAT_LOG_DEBUG);

    if (config_log_level == 0) {
        neat_log_level(ctx, NEAT_LOG_ERROR);
    } else if (config_log_level == 1){
        neat_log_level(ctx, NEAT_LOG_WARNING);
    } else {
        neat_log_level(ctx, NEAT_LOG_DEBUG);
    }

    for (i = 0; i < num_flows; i++) {
        if ((flows[i] = neat_new_flow(ctx)) == NULL) {
            fprintf(stderr, "could not initialize context\n");
            result = EXIT_FAILURE;
            goto cleanup;
        }

        // set properties
        if (neat_set_property(ctx, flows[i], arg_property ? arg_property : config_property)) {
            fprintf(stderr, "%s - error: neat_set_property\n", __func__);
            result = EXIT_FAILURE;
            goto cleanup;
        }

        ops[i].on_connected = on_connected;
        ops[i].on_error = on_error;
        ops[i].on_close = on_close;
        ops[i].userData = calloc(1, sizeof(struct stat_flow));
        neat_set_operations(ctx, flows[i], &(ops[i]));

        // wait for on_connected or on_error to be invoked
        if (neat_open(ctx, flows[i], argv[argc - 1], 80, NULL, 0) != NEAT_OK) {
            fprintf(stderr, "Could not open flow\n");
            result = EXIT_FAILURE;
        } else {
            fprintf(stderr, "Opened flow %d\n", i);
            flows_active++;
        }
    }

    neat_start_event_loop(ctx, NEAT_RUN_DEFAULT);

cleanup:
    for (i = 0; i < num_flows; i++) {
        //free((flows[i])->userData);
    }

    if (ctx != NULL) {
        neat_free_ctx(ctx);
    }

    if (arg_property) {
        free(arg_property);
    }
    fprintf(stderr, "returning with %d\n", result);
    exit(result);
}
