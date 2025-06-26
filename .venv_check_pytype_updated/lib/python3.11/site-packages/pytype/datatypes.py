"""Generic data structures and collection classes."""

import argparse
import contextlib
import itertools
from typing import TypeVar

import immutabledict

_K = TypeVar("_K")
_V = TypeVar("_V")

# Public alias for immutabledict to save users the extra import.
immutabledict = immutabledict.immutabledict
EMPTY_MAP = immutabledict()


class UnionFind:
  r"""A disjoint-set data structure for `AliasingDict`.

  This is used to record the alias information for `AliasingDict`. It is
  consist of different components. Each component will contain the names
  that represent the same thing.
    E.g., for a five-node component/tree, the representative for all the
    nodes in the component is `T`:
       T          [T] The root node and representative
      / \         [U] Its parent is `T`
     U   V        [V] Its parent is `T`
        / \       [W] Its parent is `V`
       W   X      [X] Its parent is `V`
  For performance consideration, we will compress the path each time when
  we compute the representative of a node. E.g., if we try to get the
  representative of node `W`, then the above tree will become:
      T
     /|\
    U W V
         \
          X


  Attributes:
    name2id: mapping all names to unique id.
    parent: the parent id of current unique id.
    rank: the height of the tree for corresponding component, it is an
    optimization to merge two components.
    id2name: mapping unique id to corresponding names, the reverse map of
    `name2id`.
    latest_id: the maximal allocated id.
  """

  def __init__(self):
    self.name2id = {}
    self.parent = []
    self.rank = []
    self.id2name = []
    self.latest_id = 0

  def merge_from(self, uf):
    """Merge a UnionFind into the current one."""
    for i, name in enumerate(uf.id2name):
      self.merge(name, uf.id2name[uf.parent[i]])

  def find_by_name(self, name):
    """Find the representative of a component represented by given name."""
    key = self._get_or_add_id(name)
    return self.id2name[self._find(key)]

  def merge(self, name1, name2):
    """Merge two components represented by the given names."""
    key1 = self._get_or_add_id(name1)
    key2 = self._get_or_add_id(name2)
    self._merge(key1, key2)
    return self.find_by_name(name1)

  def _get_or_add_id(self, name):
    if name not in self.name2id:
      self.name2id[name] = self.latest_id
      self.parent.append(self.latest_id)
      self.rank.append(1)
      self.id2name.append(name)
      self.latest_id += 1
    return self.name2id[name]

  def _find(self, key):
    """Find the tree root."""
    assert self.latest_id > key
    res = key
    if self.parent[key] != key:
      res = self._find(self.parent[key])
      # Compress/Optimize the search path
      self.parent[key] = res
    return res

  def _merge(self, k1, k2):
    """Merge two components."""
    assert self.latest_id > k1 and self.latest_id > k2
    s1 = self._find(k1)
    s2 = self._find(k2)
    if s1 != s2:
      if self.rank[s1] > self.rank[s2]:
        self.parent[s2] = s1
      elif self.rank[s1] < self.rank[s2]:
        self.parent[s1] = s2
      else:
        self.parent[s1] = s2
        self.rank[s2] += 1

  def __repr__(self):
    comps = []
    used = set()
    for x in self.id2name:
      if x not in used:
        comp = []
        for y in self.id2name:
          if self.find_by_name(x) == self.find_by_name(y):
            used.add(y)
            comp.append(y)
        comps.append(comp)
    return repr(comps)


class AccessTrackingDict(dict[_K, _V]):
  """A dict that tracks access of its original items."""

  def __init__(self, d=()):
    super().__init__(d)
    self.accessed_subset = {}

  def __getitem__(self, k):
    v = super().__getitem__(k)
    if k not in self.accessed_subset:
      self.accessed_subset[k] = v
    return v

  def __setitem__(self, k, v):
    if k in self:
      _ = self[k]
    # If the key is new, we don't track it.
    return super().__setitem__(k, v)

  def __delitem__(self, k):
    if k in self:
      _ = self[k]
    return super().__delitem__(k)

  def update(self, *args, **kwargs):
    super().update(*args, **kwargs)
    for d in args:
      if isinstance(d, AccessTrackingDict):
        self.accessed_subset.update(d.accessed_subset)

  @classmethod
  def merge(cls, *dicts):
    self = cls()
    for d in dicts:
      self.update(d)
    return self


class MonitorDict(dict[_K, _V]):
  """A dictionary that monitors changes to its cfg.Variable values.

  This dictionary takes arbitrary objects as keys and cfg.Variable objects as
  values. It increments a changestamp whenever a new value is added or more data
  is merged into a value. The changestamp is unaffected by the addition of
  another origin for existing data.
  """

  def __delitem__(self, name):
    raise NotImplementedError()

  @property
  def changestamp(self):
    return len(self) + sum(len(var.bindings) for var in self.values())

  @property
  def data(self):
    return itertools.chain.from_iterable(v.data for v in self.values())


class AliasingDictConflictError(Exception):

  def __init__(self, existing_name):
    super().__init__()
    self.existing_name = existing_name


class AliasingDict(dict[_K, _V]):
  """A dictionary that supports key aliasing.

  This dictionary provides a way to register aliases for a key, which are then
  treated like the key itself by getters and setters. To avoid surprising
  behavior, we raise NotImplementedError for all dict methods not explicitly
  supported; supported methods are get(), values(), items(), copy() and keys().
  """

  def __init__(self, *args, aliases: UnionFind | None = None, **kwargs):
    if aliases is not None:
      self._aliases = aliases
    elif args and isinstance(args[0], AliasingDict):
      self._aliases = args[0].aliases
    else:
      self._aliases = UnionFind()
    super().__init__(*args, **kwargs)
    for k in list(self):
      root = self._aliases.find_by_name(k)
      if root == k:
        continue
      if root not in self:
        dict.__setitem__(self, root, dict.__getitem__(self, k))
      dict.__delitem__(self, k)

  @property
  def aliases(self):
    return self._aliases

  def copy(self, *args, aliases=None, **kwargs):
    return self.__class__(self, *args, aliases=aliases, **kwargs)

  def same_name(self, name1, name2):
    return self.aliases.find_by_name(name1) == self.aliases.find_by_name(name2)

  def __contains__(self, name):
    return super().__contains__(self.aliases.find_by_name(name))

  def __setitem__(self, name, var):
    super().__setitem__(self.aliases.find_by_name(name), var)

  def __getitem__(self, name):
    return super().__getitem__(self.aliases.find_by_name(name))

  def __repr__(self):
    return f"{super().__repr__()!r}, _alias={repr(self.aliases)!r}"

  def __hash__(self):
    return hash(frozenset(self.items()))

  def get(self, name, default=None):
    # We reimplement get() because the builtin implementation doesn't play
    # nicely with aliasing.
    try:
      return self[name]
    except KeyError:
      return default

  def clear(self):
    raise NotImplementedError()

  def fromkeys(self):
    raise NotImplementedError()

  def has_key(self):
    raise NotImplementedError()

  def iteritems(self):
    raise NotImplementedError()

  def iterkeys(self):
    raise NotImplementedError()

  def itervalues(self):
    raise NotImplementedError()

  def pop(self, k):
    raise NotImplementedError()

  def popitem(self):
    raise NotImplementedError()

  def setdefault(self, k):
    raise NotImplementedError()

  def update(self):
    raise NotImplementedError()

  def viewitems(self):
    raise NotImplementedError()

  def viewkeys(self):
    raise NotImplementedError()

  def viewvalues(self):
    raise NotImplementedError()

  def merge_from(self, lam_dict, op):
    """Merge the other `AliasingDict` into current class.

    Args:
      lam_dict: The dict to merge from.
      op: The function used to merge the values.
    """
    # Merge from dict
    for key, val in lam_dict.items():
      if key in self:
        self[key] = op(self[key], val, key)
      else:
        self[key] = val
    # Merge the aliasing info
    for cur_id in range(lam_dict.aliases.latest_id):
      parent_id = lam_dict.aliases.parent[cur_id]
      cur_name = lam_dict.aliases.id2name[cur_id]
      parent_name = lam_dict.aliases.id2name[parent_id]
      if self.aliases.find_by_name(cur_name) != self.aliases.find_by_name(
          parent_name
      ):
        self.add_alias(cur_name, parent_name, op)

  def _merge(self, name1, name2, op):
    name1 = self.aliases.find_by_name(name1)
    name2 = self.aliases.find_by_name(name2)
    assert name1 != name2
    self[name1] = op(self[name1], self[name2], name1)
    dict.__delitem__(self, name2)
    root = self.aliases.merge(name1, name2)
    self._copy_item(name1, root)

  def _copy_item(self, src, tgt):
    """Assign the dict `src` value to `tgt`."""
    if src == tgt:
      return
    self[tgt] = dict.__getitem__(self, src)
    dict.__delitem__(self, src)

  def add_alias(self, alias, name, op=None):
    """Alias 'alias' to 'name'.

    After aliasing, we will think `alias` and `name`, they represent the same
    name. We will merge the values if `op` is provided.

    Args:
      alias: A string.
      name: A string.
      op: The function used to merge the values.
    """
    alias = self.aliases.find_by_name(alias)
    name = self.aliases.find_by_name(name)
    if alias == name:
      return
    elif alias in self and name in self:
      self._merge(alias, name, op)
    elif alias not in self and name not in self:
      self.aliases.merge(alias, name)
    elif alias in self:
      root = self.aliases.merge(alias, name)
      self._copy_item(alias, root)
    elif name in self:
      root = self.aliases.merge(alias, name)
      self._copy_item(name, root)


class HashableDict(AliasingDict[_K, _V]):
  """A AliasingDict subclass that can be hashed.

  Instances should not be modified. Methods that would modify the dictionary
  have been overwritten to throw an exception.
  """

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._hash = hash(frozenset(self.items()))

  def update(self):
    raise TypeError()

  def clear(self):
    raise TypeError()

  def pop(self, k):
    raise TypeError()

  def popitem(self):
    raise TypeError()

  def setdefault(self, k):
    raise TypeError()

  def __setitem__(self, name, var):
    raise TypeError()

  def __delitem__(self, y):
    raise TypeError()

  def __hash__(self):
    return self._hash


class AliasingMonitorDict(AliasingDict[_K, _V], MonitorDict[_K, _V]):
  """The dictionary that supports aliasing, lazy dict and monitor."""


class Box:
  """A mutable shared value."""

  def __init__(self, value=None):
    self._value = value

  def __get__(self, unused_obj, unused_objname):
    return self._value

  def __set__(self, unused_obj, value):
    self._value = value


class ParserWrapper:
  """Wrapper that adds arguments to a parser while recording them."""

  # This needs to be a classvar so that it is shared by subgroups
  _only = Box(None)

  def __init__(self, parser, actions=None):
    self.parser = parser
    self.actions = {} if actions is None else actions

  @contextlib.contextmanager
  def add_only(self, args):
    """Constrain the parser to only add certain arguments."""
    only = self._only
    self._only = args
    try:
      yield
    finally:
      self._only = only

  def add_argument(self, *args, **kwargs):
    if self._only and not any(arg in self._only for arg in args):
      return
    try:
      action = self.parser.add_argument(*args, **kwargs)
    except argparse.ArgumentError:
      # We might want to mask some pytype-single options.
      pass
    else:
      self.actions[action.dest] = action

  def add_argument_group(self, *args, **kwargs):
    group = self.parser.add_argument_group(*args, **kwargs)
    wrapped_group = self.__class__(group, actions=self.actions)
    return wrapped_group

  def parse_args(self, *args, **kwargs):
    return self.parser.parse_args(*args, **kwargs)

  def parse_known_args(self, *args, **kwargs):
    return self.parser.parse_known_args(*args, **kwargs)

  def error(self, *args, **kwargs):
    return self.parser.error(*args, **kwargs)
