#!/usr/bin/env python3
import asyncio
import logging
import os
from operator import attrgetter

import policy
from cib import CIB
from pib import PIB
from policy import PropertyArray

DOMAIN_SOCK = os.environ['HOME'] + '/.neat/neat_pm_socket'
PIB_DIR = 'pib/example/'
CIB_DIR = 'cib/example/'

# Make sure the socket does not already exist
try:
    os.unlink(DOMAIN_SOCK)
except OSError:
    if os.path.exists(DOMAIN_SOCK):
        raise


def process_request(json_str, num_candidates=10):
    """Process JSON requests from NEAT logic"""
    logging.debug(json_str)

    request = PropertyArray(*policy.json_to_properties(json_str))
    print('Received NEAT request: %s' % request)

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
    logging.info("%d candidates generated" % len(candidates))
    for candidate in updated_candidates:
        print(candidate, candidate.score)
    # TODO check if candidates contain the minimum src/dst/transport tuple
    return updated_candidates[:num_candidates]


class PMProtocol(asyncio.Protocol):
    def connection_made(self, transport):
        peername = transport.get_extra_info('peername')
        self.transport = transport
        self.request = ''

    def data_received(self, data):
        message = data.decode()
        self.request += message

    def eof_received(self):
        logging.info("New JSON request received (%dB)" % len(self.request))

        candidates = process_request(self.request)

        # create JSON string for NEAT logic reply
        j = [policy.properties_to_json(c) for c in candidates]
        candidates_json = '[' + ', '.join(j) + ']\n'

        data = candidates_json.encode(encoding='utf-8')
        self.transport.write(data)
        self.transport.close()


def no_loop_test():
    """
    Dummy JSON request for testing
    """
    test_request_str = '{"remote_ip": {"precedence": 2, "value": "10:54:1.23"}, "transport": {"value": "TCP"}, "MTU": {"value": [1500, Infinity]}, "low_latency": {"precedence": 2, "value": true}}'
    process_request(test_request_str)


if __name__ == "__main__":
    cib = CIB(CIB_DIR)
    profiles = PIB(PIB_DIR, file_extension='.profile')
    pib = PIB(PIB_DIR, file_extension='.policy')

    # no_loop_test()

    loop = asyncio.get_event_loop()
    # Each client connection creates a new protocol instance
    coro = loop.create_unix_server(PMProtocol, DOMAIN_SOCK)
    server = loop.run_until_complete(coro)

    print('Waiting for PM requests on {} ...'.format(server.sockets[0].getsockname()))
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        print("Quitting policy manager.")
        pass

    # Close the server
    server.close()
    loop.run_until_complete(server.wait_closed())
    loop.close()
    exit(0)
