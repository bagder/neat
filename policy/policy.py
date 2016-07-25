import copy
import json
import logging
import math
import numbers
import unittest

logging.basicConfig(format='[%(levelname)s]: %(message)s', level=logging.DEBUG)

UNDERLINE_START = '\033[4m'
UNDERLINE_END = '\033[0m'


class NEATPropertyError(Exception):
    pass


class ImmutablePropertyError(NEATPropertyError):
    pass


class InvalidPropertyError(NEATPropertyError):
    pass


def dict_to_properties(property_dict):
    """ Import a dictionary containing properties

    example: dict_to_properties({'foo':{'value':'bar', 'precedence':0}})

    """
    properties = []
    for key, attr in property_dict.items():

        if isinstance(attr, list):
            # property value is a list and we need to expand it
            for p in attr:
                properties.extend(dict_to_properties({key: p}))
        else:
            val = attr['value']
            try:
                neat_property = NEATProperty((key, val),
                                             precedence=attr.get('precedence', NEATProperty.REQUESTED),
                                             score=attr.get('score', 1.0))
            except KeyError as e:
                raise NEATPropertyError('property import failed') from e

            properties.append(neat_property)
    return properties


def json_to_properties(json_str):
    """ Import a list of JSON encoded properties

    example: json_to_properties('{"foo":{"value":"bar", "precedence":0}}')

    """
    try:
        property_dict = json.loads(json_str)
    except json.decoder.JSONDecodeError as e:
        logging.error('invalid JSON string: ' + json_str + str(e))
        return

    return dict_to_properties(property_dict)


def properties_to_json(property_array, indent=None, with_score=False):
    property_dict = dict()
    for i in property_array.values():
        property_dict.update(i.dict())
    return json.dumps(property_dict, sort_keys=True, indent=indent)


class NEATProperty(object):
    """Properties are (key,value) tuples"""

    IMMUTABLE = 2
    REQUESTED = 1
    INFORMATIONAL = 0  # TODO we don't really need this

    def __init__(self, keyval, precedence=REQUESTED, score=0):
        self.key = keyval[0]
        self._value = keyval[1]

        # new style value range
        if isinstance(self._value, (dict,)):
            try:
                self._value = [self.value['start'], self.value['end']]
            except KeyError as e:
                print(e)
                raise IndexError("Invalid property range")

        if isinstance(self.value, (tuple, list)):
            self.value = tuple((float(i) for i in self.value))
            if self.value[0] > self.value[1]:
                raise IndexError("Invalid property range")

        self.precedence = precedence
        self.score = score  # TODO maybe we should call this weight or impact factor
        # set if property was compared or updated during a lookup
        self.evaluated = False

        # TODO experimental meta data
        # attach more weight to property score
        self.weight = 1.0

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        old_value = self._value
        self._value = value
        self.evaluated = True

        if isinstance(old_value, (tuple, numbers.Number)) and isinstance(old_value, (tuple, numbers.Number)):
            # FIXME ensure that tuple values are numeric
            new_value = self._range_overlap(old_value)
            if new_value:
                self._value = new_value

    @property
    def property(self):
        return self.key, self.value

    # DELETEME
    def items(self):
        return self.property

    def dict(self, with_score=True):
        """Return a dict for JSON export"""
        json_dict = {
            self.key: dict(value=self.value, precedence=self.precedence, score=self.score, evaluated=self.evaluated)}
        return json_dict

    def __iter__(self):
        for p in self.property:
            yield p

    def __hash__(self):
        # define hash for set comparisons
        return hash(self.key)

    def eq(self, other):
        if (self.key, self.value, self.precedence) == (other.key, other.value, other.precedence):
            return True
        else:
            return False

    def __add__(self, other):
        new_prop = copy.deepcopy(self)
        new_prop.update(other)
        return new_prop

    def __and__(self, other):
        """Return true if a single value is in range, or if two ranges have an overlapping region."""
        assert isinstance(other, NEATProperty)

        if not (self.key == other.key):
            logging.debug("Property keys do not match")
            return False

        if not isinstance(other.value, tuple) and not isinstance(self.value, tuple):
            if (self.key, self.value) == (other.key, other.value):
                return self.value
            else:
                return False

        return self._range_overlap(other.value)

    def __eq__(self, other):
        """Return true if a single value is in range, or if two ranges have an overlapping region."""
        return self & other

    def _range_overlap(self, other_range):
        self_range = self.value

        if isinstance(self.value, tuple):
            if not all(isinstance(i, numbers.Number) for i in self.value):
                raise ValueError("ranges values %s are not numeric" % self.value)
        else:
            if not isinstance(self.value, numbers.Number):
                raise ValueError("value %s is not numeric" % self.value)

        # create a tuple if one of the ranges is a single numerical value
        if isinstance(other_range, numbers.Number):
            other_range = other_range, other_range
        if isinstance(self_range, numbers.Number):
            self_range = self_range, self_range

        # check if ranges have an overlapping region
        overlap = other_range[0] <= self_range[1] and other_range[1] >= self_range[0]

        if not overlap:
            return False
        else:
            # return actual range
            overlap_range = max(other_range[0], self_range[0]), min(other_range[1], self_range[1])
            if overlap_range[0] == overlap_range[1]:
                return overlap_range[0]
            else:
                return overlap_range

    def update(self, other):
        """ Update the current property value with a different one and update the score."""
        assert isinstance(other, NEATProperty)

        if not other.key == self.key:
            logging.debug("Property key mismatch")
            return

        self.evaluated = True

        if math.isnan(self.score):
            self.score = 0.0

        value_differs = not self == other

        # TODO simplify
        if (other.precedence >= self.precedence) and not (
                        other.precedence == NEATProperty.IMMUTABLE and self.precedence == NEATProperty.IMMUTABLE):

            # new precedence is higher than current precedence, update
            self.value = other.value
            self.precedence = other.precedence
            if value_differs:
                self.score += -1.0  # FIXME adjust scoring

                # logging.debug("property %s updated to %s." % (self.key, self.value))
            else:
                self.score += +1.0 + 0 * self.precedence * self.weight  # FIXME adjust scoring
                # logging.debug("property %s is already %s" % (self.key, self.value))
        elif other.precedence == NEATProperty.IMMUTABLE and self.precedence == NEATProperty.IMMUTABLE:
            if value_differs:
                self.score = -9999.0  # FIXME adjust scoring
                logging.debug("property %s|%s is _immutable_ and differs: can't update." % (self.key, self.value))
                raise ImmutablePropertyError('immutable property: %s vs. %s' % (self, other))
            else:
                self.score += +1.0  # FIXME adjust scoring
                # logging.debug("property %s is already %s" % (self.key, self.value))
        else:
            if value_differs:
                # property cannot be fulfilled - but it was not immutable
                self.score -= other.score  # FIXME adjust scoring
                logging.debug("property %s cannot be fulfilled." % (self.key))
            else:
                # update value if numeric value ranges overlap

                self.value = self == other  # comparison returns range intersection
                self.score += other.score  # FIXME adjust scoring
                logging.debug("range updated for property %s." % (self.key))

        logging.debug("property updated %s" % (self))

    def __str__(self):
        return repr(self)

    def __repr__(self):
        if isinstance(self.value, tuple):
            val_str = '%s-%s' % self.value
        else:
            val_str = str(self._value)

        keyval_str = '%s|%s' % (self.key, val_str)
        if not math.isnan(self.score):
            score_str = '%+.1f' % self.score
        else:
            score_str = ''

        if self.precedence == NEATProperty.IMMUTABLE:
            property_str = '[%s]%s' % (keyval_str, score_str)
        elif self.precedence == NEATProperty.REQUESTED:
            property_str = '(%s)%s' % (keyval_str, score_str)
        elif self.precedence == NEATProperty.INFORMATIONAL:
            property_str = '<%s>%s' % (keyval_str, score_str)
        else:
            property_str = '?%s?%s' % (keyval_str, score_str)

        if self.evaluated:
            property_str = UNDERLINE_START + property_str + UNDERLINE_END
        return property_str


class PropertyArray(dict):
    def __init__(self, *properties):
        self.add(*properties)

    def add(self, *properties):
        """
        Insert a new NEATProperty object into the array. If the property key already exists update it.
        """
        for property in properties:
            if isinstance(property, NEATProperty):
                if property.key in self.keys():
                    self[property.key].update(property)
                else:
                    self[property.key] = property
            else:
                logging.error(
                    "only NEATProperty objects may be added to PropertyDict: received %s instead" % type(property))
                return

    def __add__(self, other):
        diff = self ^ other
        inter = self & other
        return PropertyArray(*diff.values(), *inter.values())

    def __and__(self, other):
        """Return new PropertyArray containing the intersection of two PropertyArray objects."""
        inter = (self[k] + other[k] for k in self.keys() & other.keys())
        return PropertyArray(*inter)

    def __xor__(self, other):
        # return symmetric difference, i.e., non overlapping properties
        diff = [k for k in self.keys() ^ other.keys()]
        return PropertyArray(*[other[k] for k in diff if k in other], *[self[k] for k in diff if k in self])

    def intersection(self, other):
        return self & other

    @property
    def score(self):
        return sum((s.score for s in self.values() if s.evaluated))

    def dict(self):
        """ Return a dictionary containing all NEAT property attributes"""
        property_dict = dict()
        for p in self.values():
            property_dict.update(p.dict())
        return property_dict

    def __repr__(self):
        slist = []
        for i in self.values():
            slist.append(str(i))
        return '├─' + '──'.join(slist) + '─┤'


class PropertyMultiArray(dict):
    def __init__(self, *properties):
        self.add(*properties)

    def __getitem__(self, key):
        item = super().__getitem__(key)
        return [i for i in item]

    def __contains__(self, item):
        # check if item is already in the array
        return any(item.eq(property) for property in self.get(item.key, []))

    def add(self, *properties):
        """
        Insert a new NEATProperty object into the dict. If the property key already exists make it a multi property list.
        """

        for property in properties:
            if isinstance(property, list):
                for p in property:
                    self.add(p)
            elif isinstance(property, NEATProperty):
                if property.key in self.keys() and property not in self:
                    super().__getitem__(property.key).append(property)
                else:
                    self[property.key] = [property]
            elif not isinstance(property, NEATProperty):
                logging.error(
                    "only NEATProperty objects may be added to PropertyDict: received %s instead" % type(property))
                return

    def expand(self):
        pas = [PropertyArray()]
        for k, ps in self.items():
            tmp = []
            while len(pas) > 0:
                pa = pas.pop()
                for p in ps:
                    pa_copy = copy.deepcopy(pa)
                    pa_copy.add(p)
                    tmp.append(pa_copy)
            pas.extend(tmp)
        return pas

    def __repr__(self):
        slist = []
        for i in self.values():
            slist.append(str(i))
        return '├─' + '──'.join(slist) + '─┤'  # UTF8


class PropertyTests(unittest.TestCase):
    # TODO
    def test_property_logic(self):
        np2 = NEATProperty(('foo', 'bar2'), precedence=NEATProperty.IMMUTABLE)
        np1 = NEATProperty(('foo', 'bar1'), precedence=NEATProperty.REQUESTED)
        np0 = NEATProperty(('foo', 'bar0'), precedence=NEATProperty.INFORMATIONAL)

        # self.assertEqual(numeral, result)
        # unittest.main()


if __name__ == "__main__":
    import code

    np1 = NEATProperty(("MTU", {"start": 50, "end": 1000}))
    np2 = NEATProperty(("MTU", [1000, 9000]))
    np3 = NEATProperty(('foo', 'bar'))
    np4 = NEATProperty(('foo', 'bar2'))
    np5 = NEATProperty(('moo', 'rar'))
    np6 = NEATProperty(("MTU", 10000))

    pd = PropertyArray()
    pd.add(np3)
    pd.add([np1, np2], np4, np5, np6)

    pd1 = PropertyArray()
    pd1.add(NEATProperty(('remote_ip', '10.1.23.45')), NEATProperty(('MTU', 2000), precedence=2))

    pd2 = PropertyArray()
    test_request_str = '{"MTU": {"value": [1500, Infinity]}, "low_latency": {"precedence": 2, "value": true}, "remote_ip": {"precedence": 2, "value": "10:54:1.23"}, "transport": {"value": "TCP"}}'
    test = json.loads(test_request_str)
    for k, v in test.items():
        pd2.add(NEATProperty((k, v['value']), precedence=v.get('precedence', 1)))

    pd3 = PropertyArray()
    pd3.add(NEATProperty(('remote_ip', '10.1.23.45')), NEATProperty(('MTU', 100), precedence=2))

    pa = PropertyMultiArray([np1, np2], np3)
    pa.add(np3, np4, np5)
    # pa.add([np1, np2], np4, np5, np6)
    pas = pa.expand()
    print('\n'.join([str(i) for i in pa.expand()]))
    pa2 = PropertyMultiArray()

    pa3 = PropertyMultiArray()
    pa3.add([np1, np2], np3, np4, np5, np6)

    pa = PropertyArray()
    pb = PropertyArray()
    code.interact(local=locals())
