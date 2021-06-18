import jinja2
import sys
from enum import Enum
from typing import List
from random import randrange


class BasicType(Enum):
    NONE = 0

    # primitives
    INT_8 = 1
    INT_16 = 2
    INT_32 = 3
    INT_64 = 4
    CHAR = 5
    FLOAT = 6
    DOUBLE = 7

    # complex types
    STRING = 100
    IOBUF = 101

    # template<T> types
    VECTOR = 200
    OPTIONAL = 201

    # struct
    STRUCT = 300


class Type:
    """
    >>> str(Type(BasicType.INT_8))
    'std::int8_t'
    >>> str(Type(basic_type = BasicType.VECTOR, template_type = Type(BasicType.STRING)))
    'std::vector<ss::string>'
    >>> str(Type(BasicType.STRUCT, Struct('my_struct')))
    'my_struct'
    """

    def __init__(self, basic_type: BasicType, template_type=None):
        self._basic_type = basic_type
        self._template_type = template_type;

    def __str__(self):
        if self._basic_type == BasicType.INT_8:
            return "std::int8_t"
        elif self._basic_type == BasicType.INT_16:
            return "std::int16_t"
        elif self._basic_type == BasicType.INT_32:
            return "std::int32_t"
        elif self._basic_type == BasicType.INT_64:
            return "std::int64_t"
        elif self._basic_type == BasicType.CHAR:
            return "char"
        elif self._basic_type == BasicType.FLOAT:
            return "float"
        elif self._basic_type == BasicType.DOUBLE:
            return "double"
        elif self._basic_type == BasicType.STRING:
            return "ss::sstring"
        elif self._basic_type == BasicType.IOBUF:
            return "iobuf";
        elif self._basic_type == BasicType.VECTOR:
            return "std::vector<{}>".format(self._template_type)
        elif self._basic_type == BasicType.OPTIONAL:
            return "std::optional<{}>".format(self._template_type)
        elif self._basic_type == BasicType.STRUCT:
            return self._template_type._name
        else:
            raise "unknown type"


class Field:
    """
    >>> str(Field("_str_vec", Type(basic_type = BasicType.VECTOR, template_type = Type(BasicType.STRING))))
    'std::vector<ss::sstring> _str_vec;'
    """

    def __init__(self, name: str, field_type: Type):
        self._name = name
        self._type = field_type;

    def __str__(self):
        return "{} {};".format(self._type, self._name)


class Struct:
    """
    >>> str(Struct(name = 'my_struct', fields = [Field('_f1', Type(BasicType.INT_32))]))
    'struct my_struct : serde::envelope<my_struct, serde::version<10>, serde::compat_version<5>> {\\n  std::int32_t _f1;\\n};'
    """

    def __init__(self, name: str, fields: List[Field] = []):
        self._name = name
        self._fields = fields

    def __str__(self):
        return jinja2.Template("""struct {{ s._name }} : serde::envelope<{{ s._name }}, serde::version<10>, serde::compat_version<5>> {
  bool operator==({{ s._name }} const&) const = default;
{%- for s in s._fields %}
  {{ s }}
{%- endfor %}
};""").render(s=self)


FILE_TEMPLATE = """#include "serde/envelope.h"
#include "serde/serde.h"

{%- for s in structs %}
{{ s }}
{%- endfor %}

"""

my_struct = Struct(name='my_struct', fields=[Field('_f1', Type(BasicType.INT_32))])

types = [
    Type(BasicType.INT_8),
    Type(BasicType.INT_16),
    Type(BasicType.INT_32),
    Type(BasicType.INT_64),
    Type(BasicType.CHAR),
    Type(BasicType.FLOAT),
    Type(BasicType.DOUBLE),
    Type(BasicType.STRING),
    Type(BasicType.IOBUF),
    Type(BasicType.VECTOR, Type(BasicType.INT_32)),
    Type(BasicType.VECTOR, Type(BasicType.OPTIONAL, Type(BasicType.STRING))),
    Type(BasicType.OPTIONAL, Type(BasicType.STRING)),
    Type(BasicType.STRUCT, my_struct)
]


def gen_struct(i):
    ids = [randrange(len(types)) for i in range(3)]
    fields = [Field(name="_f{}".format(i), field_type=types[type_id]) for i, type_id in enumerate(ids)]
    return Struct(name="my_struct_{}".format(i), fields=fields)


def extend_type_list(struct_type: Type):
    types.extend([
        struct_type,
        Type(BasicType.OPTIONAL, struct_type),
        Type(BasicType.VECTOR, struct_type),
        Type(BasicType.OPTIONAL, Type(BasicType.VECTOR, struct_type)),
        Type(BasicType.VECTOR, Type(BasicType.OPTIONAL, struct_type))
    ])


def gen_structs():
    structs = [my_struct]
    for i in range(3):
        previous = [gen_struct(i) for i in range(len(structs), len(structs) + 3)]
        structs.extend(previous)
        for s in previous:
            extend_type_list(Type(BasicType.STRUCT, s))
    return structs


if __name__ == "__main__":
    assert len(sys.argv) == 2
    out_file = sys.argv[1]
    with open(out_file, "w") as f:
        f.write(jinja2.Template(FILE_TEMPLATE).render(structs=gen_structs()))
