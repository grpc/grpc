"""Mapping between slot / operator names.

This defines the internal constants CPython uses to map magic methods to slots
of PyTypeObject structures, and also other constants, like compare operator
mappings.
"""

import dataclasses

TYPEOBJECT_PREFIX = "tp_"
NUMBER_PREFIX = "nb_"
SEQUENCE_PREFIX = "sq_"
MAPPING_PREFIX = "mp_"


@dataclasses.dataclass
class Slot:
  """A "slot" describes a Python operator.

  In particular, it describes how a magic method (E.g. "__add__") relates to the
  opcode ("BINARY_ADD") and the C function pointer ("nb_add").

  Attributes:
    python_name: The name of the Python method. E.g. "__add__".
    c_name: The name of the C function pointer. Only use the base name, e.g. for
      tp_as_number->nb_add, use nb_add.
    function_type: Type of the C function
    index: If multiple python methods share the same function pointer (e.g.
      __add__ and __radd__), this is 0 or 1.
    opcode: The name of the opcode that CPython uses to call this function. This
      is only filled in for operators (e.g. BINARY_SUBSCR), but not for
      operations (e.g. STORE_SUBSCR).
    python_version: "2", "3", or (default) "*".
    symbol: Only filled in for operators. The corresponding symbol, e.g., "+"
      for __add__, if one exists; otherwise, a human-friendly description, e.g.,
      "item retrieval" for __getitem__.
  """

  python_name: str
  c_name: str
  function_type: str
  index: int | None = None
  opcode: str | None = None
  python_version: str | None = "*"
  symbol: str | None = None


SLOTS: list[Slot] = [
    # typeobject
    Slot("__new__", "tp_new", "new"),
    Slot("__init__", "tp_init", "init"),
    Slot("__str__", "tp_print", "print"),
    Slot("__repr__", "tp_repr", "repr", opcode="UNARY_CONVERT"),
    Slot("__hash__", "tp_hash", "hash"),
    Slot("__call__", "tp_call", "call"),
    # Note: In CPython, if tp_getattro exists, tp_getattr is never called.
    Slot("__getattribute__", "tp_getattro", "getattro"),
    Slot("__getattr__", "tp_getattro", "getattro"),
    Slot("__setattr__", "tp_setattro", "setattro"),
    Slot("__delattr__", "tp_setattro", "setattro"),
    # for Py_TPFLAGS_HAVE_ITER:
    Slot("__iter__", "tp_iter", "unary"),
    Slot("next", "tp_iternext", "next", python_version="2"),
    Slot("__next__", "tp_iternext", "next", python_version="3"),
    # for Py_TPFLAGS_HAVE_CLASS:
    Slot("__get__", "tp_descr_get", "descr_get"),
    Slot("__set__", "tp_descr_set", "descr_set"),
    Slot("__delete__", "tp_descr_set", "descr_delete"),
    Slot("__del__", "tp_del", "destructor"),
    # all typically done by __richcompare__
    Slot(
        "__cmp__", "tp_compare", "cmp", python_version="2"
    ),  # "tp_reserved" in Python 3
    Slot("__lt__", "tp_richcompare", "richcmpfunc", symbol="<"),
    Slot("__le__", "tp_richcompare", "richcmpfunc", symbol="<="),
    Slot("__eq__", "tp_richcompare", "richcmpfunc", symbol="=="),
    Slot("__ne__", "tp_richcompare", "richcmpfunc", symbol="!="),
    Slot("__gt__", "tp_richcompare", "richcmpfunc", symbol=">"),
    Slot("__ge__", "tp_richcompare", "richcmpfunc", symbol=">="),
    Slot("__richcompare__", "tp_richcompare", "richcmpfunc"),
    # number methods:
    Slot(
        "__add__",
        "nb_add",
        "binary_nb",
        index=0,
        opcode="BINARY_ADD",
        symbol="+",
    ),
    Slot("__radd__", "nb_add", "binary_nb", index=1),
    Slot(
        "__sub__",
        "nb_subtract",
        "binary_nb",
        index=0,
        opcode="BINARY_SUBTRACT",
        symbol="-",
    ),
    Slot("__rsub__", "nb_subtract", "binary_nb", index=1),
    Slot("__mul__", "nb_multiply", "binary_nb", index=0, symbol="*"),
    Slot("__rmul__", "nb_multiply", "binary_nb", index=1),
    Slot(
        "__div__",
        "nb_divide",
        "binary_nb",
        index=0,
        opcode="BINARY_DIVIDE",
        symbol="/",
    ),
    Slot("__rdiv__", "nb_divide", "binary_nb", index=1),
    Slot(
        "__mod__",
        "nb_remainder",
        "binary_nb",
        index=0,
        opcode="BINARY_MODULO",
        symbol="%",
    ),
    Slot("__rmod__", "nb_remainder", "binary_nb", index=1),
    Slot("__divmod__", "nb_divmod", "binary_nb", index=0),
    Slot("__rdivmod__", "nb_divmod", "binary_nb", index=1),
    Slot(
        "__lshift__",
        "nb_lshift",
        "binary_nb",
        index=0,
        opcode="BINARY_LSHIFT",
        symbol="<<",
    ),
    Slot("__rlshift__", "nb_lshift", "binary_nb", index=1),
    Slot(
        "__rshift__",
        "nb_rshift",
        "binary_nb",
        index=0,
        opcode="BINARY_RSHIFT",
        symbol=">>",
    ),
    Slot("__rrshift__", "nb_rshift", "binary_nb", index=1),
    Slot(
        "__and__",
        "nb_and",
        "binary_nb",
        index=0,
        opcode="BINARY_AND",
        symbol="&",
    ),
    Slot("__rand__", "nb_and", "binary_nb", index=1),
    Slot(
        "__xor__",
        "nb_xor",
        "binary_nb",
        index=0,
        opcode="BINARY_XOR",
        symbol="^",
    ),
    Slot("__rxor__", "nb_xor", "binary_nb", index=1),
    Slot(
        "__or__", "nb_or", "binary_nb", index=0, opcode="BINARY_OR", symbol="|"
    ),
    Slot("__ror__", "nb_or", "binary_nb", index=1),
    # needs Py_TPFLAGS_HAVE_CLASS:
    Slot(
        "__floordiv__",
        "nb_floor_divide",
        "binary_nb",
        index=0,
        opcode="BINARY_FLOOR_DIVIDE",
        symbol="//",
    ),
    Slot("__rfloordiv__", "nb_floor_divide", "binary_nb", index=1),
    Slot(
        "__truediv__",
        "nb_true_divide",
        "binary_nb",
        index=0,
        opcode="BINARY_TRUE_DIVIDE",
        symbol="/",
    ),
    Slot("__rtruediv__", "nb_true_divide", "binary_nb", index=1),
    Slot(
        "__pow__",
        "nb_power",
        "ternary",
        index=0,
        opcode="BINARY_POWER",
        symbol="**",
    ),
    Slot("__rpow__", "nb_power", "ternary", index=1),  # needs wrap_tenary_nb
    Slot(
        "__neg__", "nb_negative", "unary", opcode="UNARY_NEGATIVE", symbol="-"
    ),
    Slot(
        "__pos__", "nb_positive", "unary", opcode="UNARY_POSITIVE", symbol="+"
    ),
    Slot("__abs__", "nb_absolute", "unary"),
    Slot("__nonzero__", "nb_nonzero", "inquiry"),  # inverse of UNARY_NOT opcode
    Slot("__invert__", "nb_invert", "unary", opcode="UNARY_INVERT", symbol="~"),
    Slot("__coerce__", "nb_coerce", "coercion"),  # not needed
    Slot("__int__", "nb_int", "unary"),  # expects exact int as return
    Slot("__long__", "nb_long", "unary"),  # expects exact long as return
    Slot("__float__", "nb_float", "unary"),  # expects exact float as return
    Slot("__oct__", "nb_oct", "unary"),
    Slot("__hex__", "nb_hex", "unary"),
    # Added in 2.0.  These are probably largely useless.
    # (For list concatenation, use sl_inplace_concat)
    Slot(
        "__iadd__",
        "nb_inplace_add",
        "binary",
        opcode="INPLACE_ADD",
        symbol="+=",
    ),
    Slot(
        "__isub__",
        "nb_inplace_subtract",
        "binary",
        opcode="INPLACE_SUBTRACT",
        symbol="-=",
    ),
    Slot(
        "__imul__",
        "nb_inplace_multiply",
        "binary",
        opcode="INPLACE_MULTIPLY",
        symbol="*=",
    ),
    Slot(
        "__idiv__",
        "nb_inplace_divide",
        "binary",
        opcode="INPLACE_DIVIDE",
        symbol="/=",
    ),
    Slot(
        "__imod__",
        "nb_inplace_remainder",
        "binary",
        opcode="INPLACE_MODULO",
        symbol="%=",
    ),
    Slot(
        "__ipow__",
        "nb_inplace_power",
        "ternary",
        opcode="INPLACE_POWER",
        symbol="**=",
    ),
    Slot(
        "__ilshift__",
        "nb_inplace_lshift",
        "binary",
        opcode="INPLACE_LSHIFT",
        symbol="<<=",
    ),
    Slot(
        "__irshift__",
        "nb_inplace_rshift",
        "binary",
        opcode="INPLACE_RSHIFT",
        symbol=">>=",
    ),
    Slot(
        "__iand__",
        "nb_inplace_and",
        "binary",
        opcode="INPLACE_AND",
        symbol="&=",
    ),
    Slot(
        "__ixor__",
        "nb_inplace_xor",
        "binary",
        opcode="INPLACE_XOR",
        symbol="^=",
    ),
    Slot(
        "__ior__", "nb_inplace_or", "binary", opcode="INPLACE_OR", symbol="|="
    ),
    Slot(
        "__ifloordiv__",
        "nb_inplace_floor_divide",
        "binary",
        opcode="INPLACE_FLOOR_DIVIDE",
        symbol="//=",
    ),
    Slot(
        "__itruediv__",
        "nb_inplace_true_divide",
        "binary",
        opcode="INPLACE_TRUE_DIVIDE",
        symbol="/=",
    ),
    # matrix methods:
    # Added in 3.5
    Slot(
        "__matmul__",
        "nb_matrix_multiply",
        "binary_nb",
        index=0,
        opcode="BINARY_MATRIX_MULTIPLY",
        symbol="@",
    ),
    Slot("__rmatmul__", "nb_matrix_multiply", "binary_nb", index=1),
    Slot(
        "__imatmul__",
        "nb_inplace_matrix_multiply",
        "binary",
        opcode="INPLACE_MATRIX_MULTIPLY",
        symbol="@=",
    ),
    # Added in 2.5. Used whenever i acts as a sequence index (a[i])
    Slot("__index__", "nb_index", "unary"),  # needs int/long return
    # mapping
    # __getitem__: Python first tries mp_subscript, then sq_item
    # __len__: Python first tries sq_length, then mp_length
    # __delitem__: Reuses __setitem__ slot.
    Slot(
        "__getitem__",
        "mp_subscript",
        "binary",
        opcode="BINARY_SUBSCR",
        symbol="item retrieval",
    ),
    # In CPython, these two share a slot:
    Slot(
        "__delitem__",
        "mp_ass_subscript",
        "objobjargproc",
        symbol="item deletion",
    ),
    Slot(
        "__setitem__",
        "mp_ass_subscript",
        "objobjargproc",
        symbol="item assignment",
    ),
    Slot("__len__", "mp_length", "len"),
    # sequence
    Slot("__contains__", "sq_contains", "objobjproc", symbol="'in'"),
    # These sequence methods are duplicates of number or mapping methods.
    # For example, in the C API, "add" can be implemented either by sq_concat,
    # or by np_add.  Python will try both. The opcode mapping is identical
    # between the two. So e.g. the implementation of the BINARY_SUBSCR opcode in
    # Python/ceval.c will try both sq_item and mp_subscript, which is why this
    # opcode appears twice in our list.
    Slot("__add__", "sq_concat", "binary", opcode="BINARY_ADD", symbol="+"),
    Slot(
        "__mul__",
        "sq_repeat",
        "indexargfunc",
        opcode="BINARY_MULTIPLY",
        symbol="*",
    ),
    Slot(
        "__iadd__",
        "sq_inplace_concat",
        "binary",
        opcode="INPLACE_ADD",
        symbol="+=",
    ),
    Slot(
        "__imul__",
        "sq_inplace_repeat",
        "indexargfunc",
        opcode="INPLACE_MUL",
        symbol="*=",
    ),
    Slot(
        "__getitem__",
        "sq_item",
        "sq_item",
        opcode="BINARY_SUBSCR",
        symbol="item retrieval",
    ),
    Slot(
        "__setitem__", "sq_ass_slice", "sq_ass_item", symbol="item assignment"
    ),
    Slot("__delitem__", "sq_ass_item", "sq_delitem", symbol="item deletion"),
    # slices are passed as explicit slice objects to mp_subscript.
    Slot("__getslice__", "sq_slice", "sq_slice", symbol="slicing"),
    Slot("__setslice__", "sq_ass_slice", "ssizessizeobjarg"),
    Slot("__delslice__", "sq_ass_slice", "delslice"),
]


CMP_LT = 0
CMP_LE = 1
CMP_EQ = 2
CMP_NE = 3
CMP_GT = 4
CMP_GE = 5
CMP_IN = 6
CMP_NOT_IN = 7
CMP_IS = 8
CMP_IS_NOT = 9
CMP_EXC_MATCH = 10


CMP_ALWAYS_SUPPORTED = frozenset({CMP_EQ, CMP_NE, CMP_IS, CMP_IS_NOT})


EQ, NE, LT, LE, GT, GE = "==", "!=", "<", "<=", ">", ">="
COMPARES = {
    EQ: lambda x, y: x == y,
    NE: lambda x, y: x != y,
    LT: lambda x, y: x < y,
    LE: lambda x, y: x <= y,
    GT: lambda x, y: x > y,
    GE: lambda x, y: x >= y,
}


SYMBOL_MAPPING = {
    slot.python_name: slot.symbol for slot in SLOTS if slot.symbol
}


def _ReverseNameMapping():
  """__add__ -> __radd__, __mul__ -> __rmul__ etc."""
  c_name_to_reverse = {
      slot.c_name: slot.python_name for slot in SLOTS if slot.index == 1
  }
  return {
      slot.python_name: c_name_to_reverse[slot.c_name]
      for slot in SLOTS
      if slot.index == 0
  }


REVERSE_NAME_MAPPING = _ReverseNameMapping()
