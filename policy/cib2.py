import logging
import os
import operator
import json
import bisect
import itertools

from collections import ChainMap

from policy import NEATRequest, NEATCandidate, NEATProperty, PropertyArray, PropertyMultiArray, NEATPropertyError
from policy import dict_to_properties

logging.basicConfig(format='[%(levelname)s]: %(message)s', level=logging.DEBUG)


class CIBSource(object):
    cib = None

    def __init__(self, source_dict):
        self.idx = source_dict['id']
        self.root = source_dict.get('root', False)
        self.priority = source_dict.get('priority', 0)
        self.filename = source_dict.get('filename', None)
        self.description = source_dict.get('description', '')

        self._properties = PropertyMultiArray(*dict_to_properties(source_dict.get('properties')))
        self.refs = set(source_dict.get('@properties', []))

    def resolve_refs(self, path=None):
        if path is None:
            path = []
        # insert own index based on CIB source priority
        pos = bisect.bisect([self.cib[idx].priority for idx in path], self.priority)
        path.insert(pos, self.idx)

        # no more references to check
        if not (self.refs - set(path)):
            return [path]

        new_paths = []
        for idx in self.refs:
            if idx in path:
                continue
            new_paths.extend(self.cib[idx].resolve_refs(path.copy()))
        return new_paths

    @property
    def properties(self):
        paths = self.resolve_refs()

        properties = []
        import code
        code.interact(local=locals(), banner='here')
        for p in paths:
            for i in itertools.product(*(self.cib[idx]._properties.expand() for idx in p)):
                # print(i)
                properties.append(PropertyArray(*itertools.chain(*[property.values() for property in i])))

                # return ChainMap(self._properties)
        return properties


class CIB(object):
    """
    Internal representation of the CIB for testing

    """

    def __init__(self, cib_dir=None):
        self.idx = {}
        CIBSource.cib = self

        if cib_dir:
            self.load_cib(cib_dir)

    @property
    def roots(self):
        return {k: v for k, v in cib.idx.items() if v.root}

    def load_cib(self, cib_dir='cib/'):
        """Read all CIB source files from directory CIB_DIR"""
        for filename in os.listdir(cib_dir):
            if filename.endswith(('.local', '.cib', '.connection')) and not filename.startswith(('.', '#')):
                print('loading CIB source %s' % filename)
                cs = self.load_json(cib_dir + '/' + filename)
                if not cs:
                    continue
                cib_source = CIBSource(cs)
                cib_source.filename = filename

                self.register(cib_source)

    def register(self, cib_source):
        self.idx[cib_source.idx] = cib_source

    def __getitem__(self, idx):
        return self.idx[idx]

    def load_json(self, filename):
        """Read JSON file"""

        cib_file = open(filename, 'r')
        try:
            j = json.load(cib_file)
        except json.decoder.JSONDecodeError as e:
            logging.error("Could not parse CIB file " + filename)
            print(e)
            return
        return j

    def lookup(self, query, candidate_num=5):
        """CIB lookup logic implementation. Appends a list of connection candidates to the query object. TODO
        """
        candidates = []

        # check connection CIB sources first
        for idx in self.connection.keys():
            matched_properties = self[idx] & query.properties
            candidate = NEATCandidate(self[idx])
            skip_candidate = False
            for property in matched_properties.values():
                try:
                    candidate.properties.insert(property)
                except NEATPropertyError as e:
                    logging.debug(e)
                    skip_candidate = True
                    break
            if skip_candidate:
                continue
            candidates.append(candidate)

        candidates.sort(key=operator.attrgetter('score'), reverse=True)
        query.candidates = candidates[0:candidate_num]
        # TODO expand lookup to different cib types

        for idx in self.local.keys():
            matched_properties = self[idx] & query.properties
            candidate = NEATCandidate(self[idx])
            skip_candidate = False
            for property in query.properties.values():
                try:
                    candidate.properties.insert(property)
                except NEATPropertyError as e:
                    logging.debug(e)
                    skip_candidate = True
                    break
            if skip_candidate:
                continue
            candidates.append(candidate)
        candidates.sort(key=operator.attrgetter('score'), reverse=True)
        query.candidates = candidates[0:candidate_num]

    def dump(self):
        keys = list(self.entries.keys())
        keys.sort()
        for k in keys:
            print('%s: %s' % (k, self[k]))

    def __repr__(self):
        return 'CIB<>'


if __name__ == "__main__":
    cib = CIB('./cib/example2/')
    b = cib['B']
    c = cib['C']

    for idx in cib.roots:
        z = cib[idx].resolve_refs([])
        print(z)

    x = b.properties

    import code

    code.interact(local=locals(), banner='CIB')
