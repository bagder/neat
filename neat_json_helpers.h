#ifndef NEAT_JSON_HELPERS_INCLUDE_H
#define NEAT_JSON_HELPERS_INCLUDE_H

#include "neat_internal.h"
#include "neat_json_helpers.h"

#define NEAT_KEYVAL(key,value) ("{ \"" #key "\": " #value" }")

void find_enabled_protocols(json_t *json, neat_protocol_stack_type *stacks,
                            size_t *stack_count);

#endif /* ifndef NEAT_JSON_HELPERS_INCLUDE_H */
