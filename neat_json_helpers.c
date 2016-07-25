#include "neat_internal.h"
#include "neat_json_helpers.h"

#include <assert.h>

#define NEAT_TRANSPORT_PROPERTY(name, propname, protonum)		\
{									\
    #name,								\
    propname,							\
    protonum							\
}

#define NEAT_TRANSPORT(name) \
{ \
    #name, \
    "transport_" #name, \
    NEAT_STACK_ ## name \
}

struct neat_transport_property {
    const char *name;
    const char *property_name;
    neat_protocol_stack_type stack;
};

static struct neat_transport_property transports[] = {
    NEAT_TRANSPORT(TCP),
    NEAT_TRANSPORT(SCTP),
    NEAT_TRANSPORT(UDP),
    {"UDPlite", "transport_UDPlite", NEAT_STACK_UDPLITE},
};

/* Not very efficient, but it does the job.
 */
static void
find_protocols_for_precedence(json_t *json, neat_protocol_stack_type *stacks,
                       size_t *stack_count, long long precedence)
{
    // TODO: How do we check if a platform/compiler supports long long?
    // TODO: Use long instead of long long for said platforms

    size_t index = 0;
    long long prec;
    json_t *transport, *obj;

    if (*stack_count == NEAT_MAX_NUM_PROTO)
        return;

    for (int i = 0; i < NEAT_MAX_NUM_PROTO; ++i) {
        if ((transport = json_object_get(json, transports[i].property_name)) != NULL) {

            if ((obj = json_object_get(transport, "precedence")) == NULL) {
                neat_log(NEAT_LOG_DEBUG,
                         "Missing \"precedence\" in key %s, ignoring",
                         transports[i].property_name);
                continue;
            }

            if (json_typeof(obj) != JSON_INTEGER) {
                neat_log(NEAT_LOG_DEBUG,
                         "\"precedence\" in key %s specified as something else than an integer, ignoring",
                         transports[i].property_name);
                continue;
            }

            // If the precedence is different from what we're looking for, skip
            prec = json_integer_value(obj);
            if (prec != precedence)
                continue;

            // Disallow more than one immutable transport
            // The PM should ensure this won't happen
            assert(precedence != 2 || index == 0);

            stacks[(*stack_count)++] = transports[i].stack;

            if (*stack_count == NEAT_MAX_NUM_PROTO)
                return;
        }
    }
}

/* Find the enabled transport protocols within a JSON object.
 *
 * Returns the enabled transport protocols in `stacks` and the number of
 * enabled transport protocols.
 *
 * The returned array is ordered on precedence.
 */
void
find_enabled_protocols(json_t *json, neat_protocol_stack_type *stacks,
                       size_t *stack_count)
{
    NEAT_FUNC_TRACE();

    assert(stacks && stack_count);
    // assert(*stack_count >= NEAT_MAX_NUM_PROTO);

    *stack_count = 0;

    // Missing a transport protocol in the array above?
    assert(sizeof(transports) / sizeof(transports[0]) == NEAT_MAX_NUM_PROTO);

    // Pass 1: Find any mandatory (immutable) transport protocols
    find_protocols_for_precedence(json, stacks, stack_count, 2);

    // Already found a transport protocol
    if (*stack_count)
        return;

    // Pass 2: Find any requested transport protocols
    find_protocols_for_precedence(json, stacks, stack_count, 1);

    // Pass 3: Find any remaining transport protocols
    find_protocols_for_precedence(json, stacks, stack_count, 0);
}
