#!/usr/bin/env python3
import asyncore
import logging
import os
import socket

from operator import attrgetter

from cib import CIB
from pib import PIB
import policy
from policy import PropertyArray

DOMAIN_SOCK = os.environ['HOME'] + '/.neat/neat_pm_socket'
PIB_DIR = 'pib/examples/'
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
    # TODO check if candidates have src/dst/transport tuple
    return updated_candidates[:num_candidates]


class JSONHandler(asyncore.dispatcher_with_send):
    def handle_read(self):
        data = self.recv(8192)

        # convert to string and delete trailing newline and whitespace
        data = str(data, encoding='utf-8').rstrip()

        if not data:
            logging.warning('received empty data')
            return

        logging.info("New JSON request received")
        candidates = process_request(data)

        # create JSON string for NEAT logic reply
        j = [policy.properties_to_json(c) for c in candidates]
        candidates_json = '[' + ', '.join(j) + ']\n'

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
    test_request_str = '{"remote_ip": {"precedence": 2, "value": "10:54:1.23"}, "transport": {"value": "TCP"}, "MTU": {"value": [1500, Infinity]}, "low_latency": {"precedence": 2, "value": true}}'
    process_request(test_request_str)
    # import code
    # code.interact(local=locals(), banner='no loop test')


if __name__ == "__main__":
    cib = CIB(CIB_DIR)
    profiles = PIB(PIB_DIR, file_extension='.profile')
    pib = PIB(PIB_DIR, file_extension='.policy')

    no_loop_test()

    print('Waiting for PM requests...')
    server = PMServer(DOMAIN_SOCK)
    try:
        asyncore.loop()
    except KeyboardInterrupt:
        print("Quitting policy manager.")
        exit(0)
