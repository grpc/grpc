"""Constant folding optimisation for bytecode.

This optimisation adds a new pseudo-opcode, LOAD_FOLDED_CONST, which encodes the
type of a complex literal constant in its `arg` field, in a "typestruct" format
described below. There is a corresponding function, build_folded_type, which
constructs a vm type from the encoded typestruct.

The type structure stored in LOAD_FOLDED_CONST is an immutable (for easy
hashing) tree with the following elements:

('prim', <python type>) : a primitive type, e.g. ('prim', str)
(tag, types) : a collection type; 'types' represent the type params
frozenset(types): a union of types

tag   = prim | tuple | list | map | set
            the types python supports for a literal constant
types = a tuple of type | frozenset(types)
            where the size of the tuple depends on the tag, e.g ('map', (k, v))

For ease of testing and debugging there is also a simplified literal syntax to
construct and examine these typestructs, see constant_folding_test for examples.
This is less uniform, and therefore not recommended to use other than for
input/output.
"""

from typing import Any

import attrs
from pycnite import marshal as pyc_marshal
from pytype.blocks import blocks
from pytype.pyc import opcodes
from pytype.pyc import pyc


# Copied from typegraph/cfg.py
# If we have more than 64 elements in a map/list, the type variable accumulates
# too many bindings and falls back to Any. So if we find a constant with too
# many elements, we go directly to constructing an abstract type, and do not
# attempt to track keys/element positions.
MAX_VAR_SIZE = 64


class ConstantError(Exception):
  """Errors raised during constant folding."""

  def __init__(self, message, op):
    super().__init__(message)
    self.lineno = op.line
    self.message = message


#  We track constants at three levels:
#    typ: A typestruct representing the abstract type of the constant
#    elements: A list or map of top-level types
#    value: The concrete python value
#
#  'elements' is an intermediate structure that tracks individual folded
#  constants for every element in a map or list. So e.g. for the constant
#    {'x': [1, 2], 'y': 3}
#  we would have
#    typ = ('map', {str}, {('list', {int}), int})
#    value = {'x': [1, 2], 'y': 3}
#    elements = {'x': <<[1, 2]>>, 'y': <<3>>}
#  where <<x>> is the folded constant corresponding to x. This lets us
#  short-circuit pyval tracking at any level in the structure and fall back to
#  abstract types.
#
#  Note that while we could in theory just track the python value, and then
#  construct 'typ' and 'elements' at the end, that would mean recursively
#  unfolding a structure that we have just folded; the code is simpler if we
#  track elements and types at every stage.
@attrs.define
class _Constant:
  """A folded python constant."""

  typ: tuple[str, Any]
  value: Any
  elements: Any
  op: opcodes.Opcode

  @property
  def tag(self):
    return self.typ[0]

  def __repr__(self):
    return repr(self.value)


@attrs.define
class _Collection:
  """A linear collection (e.g. list, tuple, set)."""

  types: frozenset[Any]
  values: tuple[Any, ...]
  elements: tuple[Any, ...]


@attrs.define
class _Map:
  """A dictionary."""

  key_types: frozenset[Any]
  keys: tuple[Any, ...]
  value_types: frozenset[Any]
  values: tuple[Any, ...]
  elements: dict[Any, Any]


class _CollectionBuilder:
  """Build up a collection of constants."""

  def __init__(self):
    self.types = set()
    self.values = []
    self.elements = []

  def add(self, constant):
    self.types.add(constant.typ)
    self.elements.append(constant)
    self.values.append(constant.value)

  def build(self):
    return _Collection(
        types=frozenset(self.types),
        values=tuple(reversed(self.values)),
        elements=tuple(reversed(self.elements)),
    )


class _MapBuilder:
  """Build up a map of constants."""

  def __init__(self):
    self.key_types = set()
    self.value_types = set()
    self.keys = []
    self.values = []
    self.elements = {}

  def add(self, key, value):
    self.key_types.add(key.typ)
    self.value_types.add(value.typ)
    self.keys.append(key.value)
    self.values.append(value.value)
    self.elements[key.value] = value

  def build(self):
    return _Map(
        key_types=frozenset(self.key_types),
        keys=tuple(reversed(self.keys)),
        value_types=frozenset(self.value_types),
        values=tuple(reversed(self.values)),
        elements=self.elements,
    )


class _Stack:
  """A simple opcode stack."""

  def __init__(self):
    self.stack = []
    self.consts = {}

  def __iter__(self):
    return self.stack.__iter__()

  def push(self, val):
    self.stack.append(val)

  def pop(self):
    return self.stack.pop()

  def _preserve_constant(self, c):
    if c and (
        not isinstance(c.op, opcodes.LOAD_CONST)
        or isinstance(c.op, opcodes.BUILD_STRING)
    ):
      self.consts[id(c.op)] = c

  def clear(self):
    # Preserve any constants in the stack before clearing it.
    for c in self.stack:
      self._preserve_constant(c)
    self.stack = []

  def _pop_args(self, n):
    """Try to get n args off the stack for a BUILD call."""
    if len(self.stack) < n:
      # We have started a new block in the middle of constructing a literal
      # (e.g. due to an inline function call). Clear the stack, since the
      # literal is not constant.
      self.clear()
      return None
    elif n and any(x is None for x in self.stack[-n:]):
      # We have something other than constants in the arg list. Pop all the args
      # for this op off the stack, preserving constants.
      for _ in range(n):
        self._preserve_constant(self.pop())
      return None
    else:
      return [self.pop() for _ in range(n)]

  def fold_args(self, n, op):
    """Collect the arguments to a build call."""
    ret = _CollectionBuilder()
    args = self._pop_args(n)
    if args is None:
      self.push(None)
      return None

    for elt in args:
      ret.add(elt)
      elt.op.folded = op
    return ret.build()

  def fold_map_args(self, n, op):
    """Collect the arguments to a BUILD_MAP call."""
    ret = _MapBuilder()
    args = self._pop_args(2 * n)
    if args is None:
      self.push(None)
      return None

    for i in range(0, 2 * n, 2):
      v_elt, k_elt = args[i], args[i + 1]
      ret.add(k_elt, v_elt)
      k_elt.op.folded = op
      v_elt.op.folded = op
    return ret.build()

  def build_str(self, n, op):
    ret = self.fold_args(n, op)
    if ret:
      self.push(_Constant(('prim', str), '', None, op))
    else:
      self.push(None)
    return ret

  def build(self, python_type, op):
    """Build a folded type."""
    collection = self.fold_args(op.arg, op)
    if collection:
      typename = python_type.__name__
      typ = (typename, collection.types)
      try:
        value = python_type(collection.values)
      except TypeError as e:
        raise ConstantError(f'TypeError: {e.args[0]}', op) from e
      elements = collection.elements
      self.push(_Constant(typ, value, elements, op))


class _FoldedOps:
  """Mapping from a folded opcode to the top level constant that replaces it."""

  def __init__(self):
    self.folds = {}

  def add(self, op):
    self.folds[id(op)] = op.folded

  def resolve(self, op):
    f = op
    while id(f) in self.folds:
      f = self.folds[id(f)]
    return f


class _FoldConstants(pyc.CodeVisitor):
  """Fold constant literals in pyc code."""

  def visit_code(self, code: blocks.OrderedCode):
    """Visit code, folding literals."""

    def build_tuple(tup):
      out = []
      for e in tup:
        if isinstance(e, tuple):
          out.append(build_tuple(e))
        else:
          out.append(('prim', type(e)))
      return ('tuple', tuple(out))

    folds = _FoldedOps()
    for block in code.order:
      stack = _Stack()
      for op in block:
        if isinstance(op, opcodes.LOAD_CONST):
          elt = code.consts[op.arg]
          if isinstance(elt, tuple):
            typ = build_tuple(elt)
            stack.push(_Constant(typ, elt, typ[1], op))
          else:
            stack.push(_Constant(('prim', type(elt)), elt, None, op))
        elif isinstance(op, opcodes.BUILD_LIST):
          stack.build(list, op)
        elif isinstance(op, opcodes.BUILD_SET):
          stack.build(set, op)
        elif isinstance(op, opcodes.FORMAT_VALUE):
          if op.arg & pyc_marshal.Flags.FVS_MASK:
            stack.build_str(2, op)
          else:
            stack.build_str(1, op)
        elif isinstance(op, opcodes.BUILD_STRING):
          stack.build_str(op.arg, op)
        elif isinstance(op, opcodes.BUILD_MAP):
          map_ = stack.fold_map_args(op.arg, op)
          if map_:
            typ = ('map', (map_.key_types, map_.value_types))
            val = dict(zip(map_.keys, map_.values))
            stack.push(_Constant(typ, val, map_.elements, op))
        elif isinstance(op, opcodes.BUILD_CONST_KEY_MAP):
          keys = stack.pop()
          vals = stack.fold_args(op.arg, op)
          if vals:
            keys.op.folded = op
            _, t = keys.typ
            typ = ('map', (frozenset(t), vals.types))
            val = dict(zip(keys.value, vals.values))
            elements = dict(zip(keys.value, vals.elements))
            stack.push(_Constant(typ, val, elements, op))
        elif isinstance(op, opcodes.LIST_APPEND):
          elements = stack.fold_args(2, op)
          if elements:
            lst, element = elements.elements
            tag, et = lst.typ
            assert tag == 'list'
            typ = (tag, et | {element.typ})
            value = lst.value + [element.value]
            elements = lst.elements + (element,)
            stack.push(_Constant(typ, value, elements, op))
        elif isinstance(op, opcodes.LIST_EXTEND):
          elements = stack.fold_args(2, op)
          if elements:
            lst, other = elements.elements
            tag, et = lst.typ
            assert tag == 'list'
            other_tag, other_et = other.typ
            if other_tag == 'tuple':
              # Deconstruct the tuple built in opcodes.LOAD_CONST above
              other_elts = tuple(
                  _Constant(('prim', e), v, None, other.op)
                  for (_, e), v in zip(other_et, other.value)
              )
            elif other_tag == 'prim':
              if other_et == str:
                other_et = {other.typ}
                other_elts = tuple(
                    _Constant(('prim', str), v, None, other.op)
                    for v in other.value
                )
              else:
                # We have some malformed code, e.g. [*42]
                name = other_et.__name__
                msg = f'Value after * must be an iterable, not {name}'
                raise ConstantError(msg, op)
            else:
              other_elts = other.elements
            typ = (tag, et | set(other_et))
            value = lst.value + list(other.value)
            elements = lst.elements + other_elts
            stack.push(_Constant(typ, value, elements, op))
        elif isinstance(op, opcodes.MAP_ADD):
          elements = stack.fold_args(3, op)
          if elements:
            map_, key, val = elements.elements
            tag, (kt, vt) = map_.typ
            assert tag == 'map'
            typ = (tag, (kt | {key.typ}, vt | {val.typ}))
            value = {**map_.value, **{key.value: val.value}}
            elements = {**map_.elements, **{key.value: val}}
            stack.push(_Constant(typ, value, elements, op))
        elif isinstance(op, opcodes.DICT_UPDATE):
          elements = stack.fold_args(2, op)
          if elements:
            map1, map2 = elements.elements
            if map2.typ[0] != 'map':
              # We have some malformed code, e.g. {**42}
              name = map2.typ[1].__name__
              msg = f'Value after ** must be an mapping, not {name}'
              raise ConstantError(msg, op)
            tag1, (kt1, vt1) = map1.typ
            tag2, (kt2, vt2) = map2.typ
            assert tag1 == tag2 == 'map'
            typ = (tag1, (kt1 | kt2, vt1 | vt2))
            value = {**map1.value, **map2.value}
            elements = {**map1.elements, **map2.elements}
            stack.push(_Constant(typ, value, elements, op))
        else:
          # If we hit any other bytecode, we are no longer building a literal
          # constant. Insert a None as a sentinel to the next BUILD op to
          # not fold itself.
          stack.push(None)

      # Clear the stack to save any folded constants before exiting the block
      stack.clear()

      # Now rewrite the block to replace folded opcodes with a single
      # LOAD_FOLDED_CONSTANT opcode.
      out = []
      for op in block:
        if id(op) in stack.consts:
          t = stack.consts[id(op)]
          arg = t
          argval = t
          o = opcodes.LOAD_FOLDED_CONST(
              op.index, op.line, op.endline, op.col, op.endcol, arg, argval
          )
          o.next = op.next
          o.target = op.target
          o.block_target = op.block_target
          o.code = op.code
          op.folded = o
          folds.add(op)
          out.append(o)
        elif op.folded:
          folds.add(op)
        else:
          out.append(op)
      block.code = out

    # Adjust 'next' and 'target' pointers to account for folding.
    for op in code.code_iter:
      if op.next:
        op.next = folds.resolve(op.next)
      if op.target:
        op.target = folds.resolve(op.target)
    return code


def to_literal(typ, always_tuple=False):
  """Convert a typestruct item to a simplified form for ease of use."""

  def expand(params):
    return (to_literal(x) for x in params)

  def union(params):
    ret = tuple(sorted(expand(params), key=str))
    if len(ret) == 1 and not always_tuple:
      (ret,) = ret  # pylint: disable=self-assigning-variable
    return ret

  tag, params = typ
  if tag == 'prim':
    return params
  elif tag == 'tuple':
    vals = tuple(expand(params))
    return (tag, *vals)
  elif tag == 'map':
    k, v = params
    return (tag, union(k), union(v))
  else:
    return (tag, union(params))


def from_literal(tup):
  """Convert from simple literal form to the more uniform typestruct."""

  def expand(vals):
    return [from_literal(x) for x in vals]

  def union(vals):
    if not isinstance(vals, tuple):
      vals = (vals,)
    v = expand(vals)
    return frozenset(v)

  if not isinstance(tup, tuple):
    return ('prim', tup)
  elif isinstance(tup[0], str):
    tag, *vals = tup
    if tag == 'prim':
      return tup
    elif tag == 'tuple':
      params = tuple(expand(vals))
      return (tag, params)
    elif tag == 'map':
      k, v = vals
      return (tag, (union(k), union(v)))
    else:
      (vals,) = vals  # pylint: disable=self-assigning-variable
      return (tag, union(vals))
  else:
    return tuple(expand(tup))


def fold_constants(code: blocks.OrderedCode):
  """Fold all constant literals in the bytecode into LOAD_FOLDED_CONST ops."""
  return pyc.visit(code, _FoldConstants())


def build_folded_type(ctx, state, const):
  """Convert a typestruct to a vm type."""

  def typeconst(t):
    """Create a constant purely to hold types for a recursive call."""
    return _Constant(t, None, None, const.op)

  def build_pyval(state, const):
    if const.value is not None and const.tag in ('prim', 'tuple'):
      return state, ctx.convert.constant_to_var(const.value)
    else:
      return build_folded_type(ctx, state, const)

  def expand(state, elements):
    vs = []
    for e in elements:
      state, v = build_pyval(state, e)
      vs.append(v)
    return state, vs

  def join_types(state, ts):
    xs = [typeconst(t) for t in ts]
    state, vs = expand(state, xs)
    val = ctx.convert.build_content(vs)
    return state, val

  def collect(state, convert_type, params):
    state, t = join_types(state, params)
    ret = ctx.convert.build_collection_of_type(state.node, convert_type, t)
    return state, ret

  def collect_tuple(state, elements):
    state, vs = expand(state, elements)
    return state, ctx.convert.build_tuple(state.node, vs)

  def collect_list(state, params, elements):
    if elements is None:
      return collect(state, ctx.convert.list_type, params)
    elif len(elements) < MAX_VAR_SIZE:
      state, vs = expand(state, elements)
      return state, ctx.convert.build_list(state.node, vs)
    else:
      # Without constant folding we construct a variable wrapping every element
      # in the list and store it; however, we cannot retrieve them all. So as an
      # optimisation, we will add the first few elements as pyals, then add one
      # element for every contained type, and rely on the fact that the tail
      # elements will contribute to the overall list type, but will not be
      # retrievable as pyvals.
      # TODO(b/175443170): We should use a smaller MAX_SUBSCRIPT cutoff; this
      # behaviour is unrelated to MAX_VAR_SIZE (which limits the number of
      # distinct bindings for the overall typevar).
      n = MAX_VAR_SIZE - len(params) - 1
      elts = elements[:n] + tuple(typeconst(t) for t in params)
      state, vs = expand(state, elts)
      return state, ctx.convert.build_list(state.node, vs)

  def collect_map(state, params, elements):
    m_var = ctx.convert.build_map(state.node)
    m = m_var.data[0]
    # Do not forward the state while creating dict literals.
    node = state.node
    # We want a single string type to store in the  Dict.K type param.
    # Calling set_str_item on every k/v pair will lead to a type param with a
    # lot of literal strings as bindings, causing potentially severe performance
    # issues down the line.
    str_key = ctx.convert.str_type.instantiate(node)
    if elements is not None and len(elements) < MAX_VAR_SIZE:
      for k, v in elements.items():
        _, v = build_pyval(state, v)
        k_var = ctx.convert.constant_to_var(k)
        m.setitem(node, k_var, v)
        if isinstance(k, str):
          m.merge_instance_type_params(node, str_key, v)
        else:
          m.merge_instance_type_params(node, k_var, v)
    else:
      # Treat a too-large dictionary as {Union[keys] : Union[vals]}. We could
      # store a subset of the k/v pairs, as with collect_list, but for
      # dictionaries it is less obvious which subset we should be storing.
      # Perhaps we could create one variable per unique value type, and then
      # store every key in the pyval but reuse the value variables.
      k_types, v_types = params
      _, v = join_types(state, v_types)
      for t in k_types:
        _, k = build_folded_type(ctx, state, typeconst(t))
        m.setitem(node, k, v)
        m.merge_instance_type_params(node, k, v)
    return state, m_var

  tag, params = const.typ
  if tag == 'prim':
    if const.value:
      return state, ctx.convert.constant_to_var(const.value)
    else:
      val = ctx.convert.primitive_instances[params]
      return state, val.to_variable(state.node)
  elif tag == 'list':
    return collect_list(state, params, const.elements)
  elif tag == 'set':
    return collect(state, ctx.convert.set_type, params)
  elif tag == 'tuple':
    # If we get a tuple without const.elements, construct it from the type.
    # (e.g. this happens with a large dict with tuple keys)
    if not const.elements:
      elts = tuple(typeconst(t) for t in params)
    else:
      elts = const.elements
    return collect_tuple(state, elts)
  elif tag == 'map':
    return collect_map(state, params, const.elements)
  else:
    assert False, ('Unexpected type tag:', const.typ)
