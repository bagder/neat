#!/usr/bin/env python3
import asyncore
import logging
import os
import socket

from operator import attrgetter

from cib import CIB
from policy import PIB, NEATProperty, NEATPolicy, NEATRequest

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

    request = NEATRequest()
    request.properties.insert_json(json_str)
    print('received NEAT request: %s' % str(request.properties))

    # main lookup sequence
    profiles._lookup(request.properties, remove_matched=True, apply=True)
    cib.lookup(request)
    pib.lookup_all(request.candidates)

    request.candidates.sort(key=attrgetter('score'), reverse=True)
    candidates_json = '[' + ', '.join([candidate.properties.json() for candidate in request.candidates]) + ']'

    logging.info("%d JSON candidates generated" % len(request.candidates))
    request.dump()

    return candidates_json


class JSONHandler(asyncore.dispatcher_with_send):
    def handle_read(self):
        data = self.recv(8192)

        # convert to string and delete trailing newline and whitespace
        data = str(data, encoding='utf-8').rstrip()

        logging.info("new JSON request received")
        candidates = process_request(data)
        if candidates:
            candidates += '\n'
            self.send(candidates.encode(encoding='utf-8'))


class PMServer(asyncore.dispatcher):
    def __init__(self, domain_sock):
        asyncore.dispatcher.__init__(self)
        self.create_socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.bind(domain_sock)
        self.listen(5)

    def handle_accepted(self, sock, addr):
        print('Incoming connection')
        handler = JSONHandler(sock)


if __name__ == "__main__":
    cib = CIB('cib/example/')
    profiles = PIB('pib/profiles/')
    pib = PIB('pib/examples/')

    print('Waiting for PM requests...')
    server = PMServer(DOMAIN_SOCK)
    asyncore.loop()

    # test_request = '{"MTU": {"value": [1500, Infinity]}, "low_latency": {"precedence": 2, "value": true}, "remote_ip": {"precedence": 2, "value": "10.1.23.45"}, "transport_TCP": {"value": true}}'
    # process_request(test_request)
