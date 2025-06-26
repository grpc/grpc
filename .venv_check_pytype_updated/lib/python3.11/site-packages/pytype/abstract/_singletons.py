"""Singleton abstract values."""

import logging

from pytype import datatypes
from pytype.abstract import _base
from pytype.pytd import escape
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg

log = logging.getLogger(__name__)


class Unknown(_base.BaseValue):
  """Representation of unknown values.

  These are e.g. the return values of certain functions (e.g. eval()). They
  "adapt": E.g. they'll respond to get_attribute requests by creating that
  attribute.

  Attributes:
    members: Attributes that were written or read so far. Mapping of str to
      cfg.Variable.
    owner: cfg.Binding that contains this instance as data.
  """

  _current_id = 0

  # For simplicity, Unknown doesn't emulate descriptors:
  IGNORED_ATTRIBUTES = ["__get__", "__set__", "__getattribute__"]

  def __init__(self, ctx):
    name = escape.unknown(Unknown._current_id)
    super().__init__(name, ctx)
    self.members = datatypes.MonitorDict()
    self.owner = None
    Unknown._current_id += 1
    self.class_name = self.name
    self._calls = []
    log.info("Creating %s", self.class_name)

  def compute_mro(self):
    return self.default_mro()

  def get_fullhash(self, seen=None):
    # Unknown needs its own implementation of get_fullhash to ensure equivalent
    # Unknowns produce the same hash. "Equivalent" in this case means "has the
    # same members," so member names are used in the hash instead of id().
    return hash((type(self),) + tuple(sorted(self.members)))

  @classmethod
  def _to_pytd(cls, node, v):
    if isinstance(v, cfg.Variable):
      return pytd_utils.JoinTypes(cls._to_pytd(node, t) for t in v.data)
    elif isinstance(v, Unknown):
      # Do this directly, and use NamedType, in case there's a circular
      # dependency among the Unknown instances.
      return pytd.NamedType(v.class_name)
    else:
      return v.to_pytd_type(node)

  @classmethod
  def _make_params(cls, node, args, kwargs):
    """Convert a list of types/variables to pytd parameters."""

    def _make_param(name, p):
      return pytd.Parameter(
          name,
          cls._to_pytd(node, p),
          kind=pytd.ParameterKind.REGULAR,
          optional=False,
          mutated_type=None,
      )

    pos_params = tuple(_make_param(f"_{i+1}", p) for i, p in enumerate(args))
    key_params = tuple(_make_param(name, p) for name, p in kwargs.items())
    return pos_params + key_params

  def get_special_attribute(self, node, name, valself):
    del node, valself
    if name in self.IGNORED_ATTRIBUTES:
      return None
    if name in self.members:
      return self.members[name]
    new = self.ctx.convert.create_new_unknown(
        self.ctx.root_node, action="getattr_" + self.name + ":" + name
    )
    # We store this at the root node, even though we only just created this.
    # From the analyzing point of view, we don't know when the "real" version
    # of this attribute (the one that's not an unknown) gets created, hence
    # we assume it's there since the program start.  If something overwrites it
    # in some later CFG node, that's fine, we'll then work only with the new
    # value, which is more accurate than the "fictional" value we create here.
    self.ctx.attribute_handler.set_attribute(
        self.ctx.root_node, self, name, new
    )
    return new

  def call(self, node, func, args, alias_map=None):
    ret = self.ctx.convert.create_new_unknown(
        node, source=self.owner, action="call:" + self.name
    )
    self._calls.append((args.posargs, args.namedargs, ret))
    return node, ret

  def argcount(self, _):
    return 0

  def to_variable(self, node):
    v = self.ctx.program.NewVariable()
    val = v.AddBinding(self, source_set=[], where=node)
    self.owner = val
    self.ctx.vm.trace_unknown(self.class_name, val)
    return v

  def to_structural_def(self, node, class_name):
    """Convert this Unknown to a pytd.Class."""
    self_param = (
        pytd.Parameter(
            "self", pytd.AnythingType(), pytd.ParameterKind.REGULAR, False, None
        ),
    )
    starargs = None
    starstarargs = None

    def _make_sig(args, kwargs, ret):
      return pytd.Signature(
          self_param + self._make_params(node, args, kwargs),
          starargs,
          starstarargs,
          return_type=Unknown._to_pytd(node, ret),
          exceptions=(),
          template=(),
      )

    calls = tuple(
        pytd_utils.OrderedSet(
            _make_sig(args, kwargs, ret) for args, kwargs, ret in self._calls
        )
    )
    if calls:
      methods = (pytd.Function("__call__", calls, pytd.MethodKind.METHOD),)
    else:
      methods = ()
    return pytd.Class(
        name=class_name,
        keywords=(),
        bases=(pytd.NamedType("builtins.object"),),
        methods=methods,
        constants=tuple(
            pytd.Constant(name, Unknown._to_pytd(node, c))
            for name, c in self.members.items()
        ),
        classes=(),
        decorators=(),
        slots=None,
        template=(),
    )

  def instantiate(self, node, container=None):
    return self.to_variable(node)


class Singleton(_base.BaseValue):
  """A Singleton class must only be instantiated once.

  This is essentially an ABC for Unsolvable, Empty, and others.
  """

  _instance = None

  def __new__(cls, *args, **kwargs):
    # If cls is a subclass of a subclass of Singleton, cls._instance will be
    # filled by its parent. cls needs to be given its own instance.
    if not cls._instance or type(cls._instance) != cls:  # pylint: disable=unidiomatic-typecheck
      log.debug("Singleton: Making new instance for %s", cls)
      cls._instance = super().__new__(cls)  # pylint: disable=no-value-for-parameter
    return cls._instance

  def get_special_attribute(self, node, name, valself):
    del name, valself
    return self.to_variable(node)

  def compute_mro(self):
    return self.default_mro()

  def call(self, node, func, args, alias_map=None):
    del func, args
    return node, self.to_variable(node)

  def instantiate(self, node, container=None):
    return self.to_variable(node)


class Empty(Singleton):
  """An empty value.

  These values represent items extracted from empty containers. Because of false
  positives in flagging containers as empty (consider:
    x = []
    def initialize():
      populate(x)
    def f():
      iterate(x)
  ), we treat these values as placeholders that we can do anything with, similar
  to Unsolvable, with the difference that they eventually convert to
  NothingType so that cases in which they are truly empty are discarded (see:
    x = ...  # type: List[nothing] or Dict[int, str]
    y = [i for i in x]  # The type of i is int; y is List[int]
  ). On the other hand, if Empty is the sole type candidate, we assume that the
  container was populated elsewhere:
    x = []
    def initialize():
      populate(x)
    def f():
      return x[0]  # Oops! The return type should be Any rather than nothing.
  The nothing -> anything conversion happens in
  convert.Converter._function_to_def and tracer_vm.CallTracer.pytd_for_types.
  """

  def __init__(self, ctx):
    super().__init__("empty", ctx)


class Deleted(Empty):
  """Assigned to variables that have del called on them."""

  def __init__(self, line, ctx):
    super().__init__(ctx)
    self.line = line
    self.name = "deleted"

  def get_special_attribute(self, node, name, valself):
    del name, valself  # unused
    return self.ctx.new_unsolvable(node)


class Unsolvable(Singleton):
  """Representation of value we know nothing about.

  Unlike "Unknowns", we don't treat these as solvable. We just put them
  where values are needed, but make no effort to later try to map them
  to named types. This helps conserve memory where creating and solving
  hundreds of unknowns would yield us little to no information.

  This is typically a singleton. Since unsolvables are indistinguishable, we
  only need one.
  """

  IGNORED_ATTRIBUTES = ["__get__", "__set__", "__getattribute__"]

  # Since an unsolvable gets generated e.g. for every unresolved import, we
  # can have multiple circular Unsolvables in a class' MRO. Treat those special.
  SINGLETON = True

  def __init__(self, ctx):
    super().__init__("unsolveable", ctx)

  def get_special_attribute(self, node, name, _):
    # Overrides Singleton.get_special_attributes.
    if name in self.IGNORED_ATTRIBUTES:
      return None
    else:
      return self.to_variable(node)

  def argcount(self, _):
    return 0


class Null(Singleton):
  """A NULL value pushed onto the data stack."""

  def __init__(self, ctx):
    super().__init__("null", ctx)
