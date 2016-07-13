import logging
import os
import operator
import json
import bisect
import itertools

from collections import ChainMap

import shutil

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

        self.properties = PropertyMultiArray(*dict_to_properties(source_dict.get('properties')))
        self.refs = set(source_dict.get('@next', []))

    def resolve_refs(self, path=None):
        if path is None:
            path = []
        # insert own index based on CIB source priority to resolve overlapping properties later
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

    def expand(self):
        paths = self.resolve_refs()
        properties = []
        for p in paths:
            expanded_properties = (self.cib[idx].properties.expand() for idx in p)
            for i in itertools.product(*expanded_properties):
                # print(i)
                properties.append(ChainMap(*i))
        return properties

    def __repr__(self):
        s = ''
        return "%s @refs%s" % (self.properties, self.refs)


def load_json(filename):
    """Read CIB source from JSON file"""

    cib_file = open(filename, 'r')
    try:
        j = json.load(cib_file)
    except json.decoder.JSONDecodeError as e:
        logging.error("Could not parse CIB file " + filename)
        print(e)
        return
    return j


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
                cs = load_json(cib_dir + '/' + filename)
                if not cs:
                    continue
                cib_source = CIBSource(cs)
                cib_source.filename = filename

                self.register(cib_source)

    def register(self, cib_source):
        self.idx[cib_source.idx] = cib_source

    def __getitem__(self, idx):
        return self.idx[idx]

    def lookup(self, query, candidate_num=5):
        """
        CIB lookup logic implementation. Appends a list of connection candidates to the query object. TODO

        """
        candidates = []

    @property
    def entries(self):
        """
        Returns a generator containing all expanded root CIB sources

        """
        for k, v in self.roots.items():
            # expand all cib sources
            for p in v.expand():
                yield PropertyArray(*[p[k] for k in p.keys()])

    def dump(self, show_all=False):
        ts = shutil.get_terminal_size()
        tcol = ts.columns
        if show_all:
            items = self.idx.items()
        else:
            items = self.roots.items()

        print("=" * int((tcol - 11) / 2) + " CIB START " + "=" * int((tcol - 11) / 2))
        for k, v in items:
            # expand all cib sources
            for p in v.expand():
                print('%s: %s' % (k, PropertyArray(*[p[k] for k in p.keys()])))
        print("=" * int((tcol - 9) / 2) + " CIB END " + "=" * int((tcol - 9) / 2))

    def __repr__(self):
        return 'CIB<%d>' % (len(cib.idx))


if __name__ == "__main__":
    cib = CIB('./cib/example2/')
    b = cib['B']
    c = cib['C']

    for idx in cib.roots:
        z = cib[idx].resolve_refs([])
        print(z)

    x = b.expand()

    import code

    code.interact(local=locals(), banner='CIB')
