"""Trace function arguments, return values and calls to other functions."""

import dataclasses
from typing import Any

from pytype.pytd import escape
from pytype.pytd import pytd
from pytype.pytd import pytd_utils


@dataclasses.dataclass
class Attr:
  name: str
  node_type: str
  type: Any
  attrib: str
  location: str


@dataclasses.dataclass
class Arg:
  name: str
  node_type: str
  type: Any


@dataclasses.dataclass
class Param:
  name: str
  type: Any


@dataclasses.dataclass
class Call:
  function_id: str
  args: list[Arg]
  location: str


@dataclasses.dataclass
class Function:
  id: str
  params: list[Any] = dataclasses.field(default_factory=list)
  param_attrs: list[Any] = dataclasses.field(default_factory=list)
  local_attrs: list[Any] = dataclasses.field(default_factory=list)
  calls: list[Any] = dataclasses.field(default_factory=list)
  ret: Any = dataclasses.field(default=None)
  location: Any = dataclasses.field(default=None)


def unknown_to_any(typename):
  if escape.UNKNOWN in typename:
    return 'typing.Any'
  return typename


def unwrap_type(typ):
  if isinstance(typ, (pytd.ClassType, pytd.NamedType)):
    typ_name = typ.name
  elif isinstance(typ, pytd.UnionType):
    typ_name = 'Union[' + ', '.join(unwrap_type(t) for t in typ.type_list) + ']'
  elif isinstance(typ, pytd.AnythingType):
    typ_name = 'typing.Any'
  else:
    typ_name = pytd_utils.Print(typ)
  return unknown_to_any(typ_name)


def get_function_params(pytd_fn):
  """Collect function param types from pytype."""
  # We have turned call records on in the indexer, so a function will have a
  # pytd signature for every tuple of call args. Here we iterate through those
  # signatures and set every param's type to the union of every non-"unknown"
  # call type for that param.
  params = {}
  for sig in pytd_fn.signatures:
    for p in sig.params:
      if p.name not in params:
        params[p.name] = []
      if escape.UNKNOWN not in str(p.type):
        params[p.name].append(p.type)
  for k in params:
    params[k] = pytd_utils.JoinTypes(params[k])
  return [(k, unwrap_type(v)) for k, v in params.items()]


class FunctionMap:
  """Collect a map of function types and outbound callgraph edges."""

  def __init__(self, index):
    self.index = index
    self.fmap = self.init_from_index(index)

  def pytd_of_fn(self, f):
    """Get the pytype pytd function definition."""
    if f.data and f.data[0]:
      d = f.data[0][0]
      try:
        return self.index.get_pytd_def(d, f.name)
      except:  # pylint: disable=bare-except
        # We sometimes get Instance(PyTDClass(str)) here, which throws an
        # exception in get_pytd_def, possibly due to other earlier problems.
        # Don't crash the indexer if this happens.
        return None
    else:
      # TODO(mdemello): log this
      return None

  def init_from_index(self, index):
    """Initialize the function map."""
    out = {}
    fn_defs = [(k, v) for k, v in index.defs.items() if v.typ == 'FunctionDef']
    for fn_id, fn in fn_defs:
      pytd_fn = self.pytd_of_fn(fn)
      if isinstance(pytd_fn, pytd.Function):
        params = get_function_params(pytd_fn)
      else:
        # Sometimes pytype cannot infer the type of a function, and falls back
        # to Any. Don't crash the indexer if this happens.
        params = []
      params = [Param(name, typ) for name, typ in params]
      ret = index.envs[fn_id].ret
      if fn_id in index.locs:
        location = index.locs[fn_id][-1].location
      else:
        location = None
      out[fn_id] = Function(
          id=fn_id, params=params, ret=ret, location=location)
    # Add a slot for "module" to record function calls made at top-level
    out['module'] = Function(id='module')
    return out

  def add_attr(self, ref, defn):
    """Add an attr access within a function body."""
    attrib = ref.name
    scope = ref.ref_scope
    if scope not in self.fmap:
      # This call was not within a function body.
      return

    try:
      d = self.index.envs[scope].env[ref.target]
    except KeyError:
      return

    typename = unknown_to_any(defn.typename)
    attr_access = Attr(
        name=d.name,
        node_type=d.typ,
        type=typename,
        attrib=attrib,
        location=ref.location)
    fn = self.fmap[scope]
    if attr_access.node_type == 'Param':
      fn.param_attrs.append(attr_access)
    else:
      fn.local_attrs.append(attr_access)

  def add_param_def(self, ref, defn):
    """Add a function parameter definition."""
    fn = self.fmap[ref.ref_scope]
    for param in fn.params:
      # Don't override a type inferred from call sites.
      if param.name == defn.name and param.type in ('nothing', 'typing.Any'):
        param.type = unwrap_type(self.index.get_pytd(ref.data[0]))
        break

  def add_link(self, ref, defn):
    if ref.typ == 'Attribute':
      self.add_attr(ref, defn)
    if defn.typ == 'Param':
      self.add_param_def(ref, defn)

  def add_call(self, call):
    """Add a function call."""
    scope = call.scope
    if scope not in self.fmap:
      # This call was not within a function body.
      return
    env = self.index.envs[scope]
    args = []
    for name in call.args:
      if name in env.env:
        defn = env.env[name]
        node_type = defn.typ
        typename = unknown_to_any(defn.typename)
      else:
        node_type = None
        typename = 'typing.Any'
      args.append(Arg(name, node_type, typename))
    self.fmap[scope].calls.append(
        Call(call.func, args, call.location))


def collect_function_map(index):
  """Track types and outgoing calls within a function."""

  fns = FunctionMap(index)

  # Collect methods and attribute accesses
  for ref, defn in index.links:
    fns.add_link(ref, defn)

  # Collect function calls
  for call in index.calls:
    fns.add_call(call)

  return fns.fmap
