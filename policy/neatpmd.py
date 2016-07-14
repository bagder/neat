#!/usr/bin/env python3
import asyncore
import logging
import os
import socket

from operator import attrgetter

from cib import CIB
from pib import PIB, NEATPolicy
from policy import NEATProperty, PropertyArray, json_to_properties, properties_to_json

DOMAIN_SOCK = os.environ['HOME'] + '/.neat/neat_pm_socket'

# Make sure the socket does not already exist
try:
    os.unlink(DOMAIN_SOCK)
except OSError:
    if os.path.exists(DOMAIN_SOCK):
        raise


def process_request(json_str):
    """Process JSON requests from NEAT logic"""
    logging.debug(json_str)

    request = PropertyArray(*json_to_properties(json_str))
    print('received NEAT request: %s' % request)

    # main lookup sequence
    print('Profile lookup...')
    updated_requests = profiles.lookup(request)

    candidates = []

    print('CIB lookup...')
    for ur in updated_requests:
        candidates.extend(cib.lookup(ur))

    print('PIB lookup...')
    updated_candidates = []
    for candidate in candidates:
        updated_candidates.extend(pib.lookup(candidate))

    updated_candidates.sort(key=attrgetter('score'), reverse=True)


    # create JSON string for NEAT logic reply
    j = [properties_to_json(c) for c in updated_candidates]
    candidates_json = '[' + ', '.join(j) + ']'

    logging.info("%d JSON candidates generated" % len(updated_candidates))
    for candidate in updated_candidates:
        print(candidate)
    return candidates_json


class JSONHandler(asyncore.dispatcher_with_send):
    def handle_read(self):
        data = self.recv(8192)

        # convert to string and delete trailing newline and whitespace
        data = str(data, encoding='utf-8').rstrip()

        if not data:
            logging.debug('empty data')
            return

        logging.info("new JSON request received")
        # try:
        candidates_json = process_request(data)
        # except Exception as e:
        #    print("Error processing request: ")
        #    print(e)
        #    return

        if candidates_json:
            candidates_json += '\n'
            self.send(candidates_json.encode(encoding='utf-8'))


class PMServer(asyncore.dispatcher):
    def __init__(self, domain_sock):
        asyncore.dispatcher.__init__(self)
        self.create_socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.bind(domain_sock)
        self.listen(5)

    def handle_accepted(self, sock, addr):
        logging.debug('Incoming connection')
        handler = JSONHandler(sock)


def no_loop_test():
    test_request_str = '{"MTU": {"value": [1500, Infinity]}, "low_latency": {"precedence": 2, "value": true}, "remote_ip": {"precedence": 2, "value": "10:54:1.23"}, "transport": {"value": "TCP"}}'
    process_request(test_request_str)


if __name__ == "__main__":
    cib = CIB('cib/example/')
    profiles = PIB('pib/examples/', file_extension='.profile')
    pib = PIB('pib/examples/', file_extension='.policy')

    no_loop_test()
    import code
    code.interact(local=locals(), banner='no loop')

    print('Waiting for PM requests...')
    server = PMServer(DOMAIN_SOCK)
    asyncore.loop()
