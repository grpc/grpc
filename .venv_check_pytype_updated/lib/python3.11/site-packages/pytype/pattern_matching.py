"""Support for pattern matching."""

import collections
import dataclasses
import enum
from typing import Optional, Union, cast

from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.pyc import opcodes
from pytype.pytd import slots
from pytype.typegraph import cfg


# Type aliases

# Tri-state boolean for match case returns.
# True = always match, False = never match, None = sometimes match
_MatchSuccessType = Optional[bool]

# Value held in an Option; enum members are stored as strings since pytd enums
# do not generate a special class.
_Value = Union[str, abstract.BaseValue]


def _get_class_values(cls: abstract.Class) -> list[_Value] | None:
  """Get values for a class with a finite set of instances."""
  if not isinstance(cls, abstract.Class):
    return None
  if cls.is_enum:
    return _get_enum_members(cls)
  else:
    return None


def _get_enum_members(enum_cls: abstract.Class) -> list[str]:
  """Get members of an enum class."""
  if isinstance(enum_cls, abstract.PyTDClass):
    # We don't construct a special class for pytd enums, so we have to get the
    # enum members manually here.
    members = []
    for k, v in enum_cls.members.items():
      if all(d.cls == enum_cls for d in v.data):
        members.append(f"{enum_cls.full_name}.{k}")
    return members
  else:
    return list(enum_cls.get_enum_members(qualified=True))


def _is_enum_match(
    match_var: cfg.Variable, case_val: abstract.BaseValue
) -> bool:
  """Is the current case part of an enum match?"""
  try:
    match_val = abstract_utils.get_atomic_value(match_var)
  except abstract_utils.ConversionError:
    return False
  if not (
      isinstance(match_val, abstract.Instance)
      and isinstance(match_val.cls, abstract.Class)
      and match_val.cls.is_enum
  ):
    return False
  if not (
      isinstance(case_val, abstract.Instance) and case_val.cls == match_val.cls
  ):
    return False
  return True


def _is_literal_match(match_var: cfg.Variable) -> bool:
  """Is the current case part of a literal match?"""
  return all(isinstance(x, abstract.ConcreteValue) for x in match_var.data)


class _Option:
  """Holds a match type option and any associated values."""

  def __init__(self, typ=None):
    self.typ: abstract.BaseValue = typ
    self.values: set[_Value] = set()
    self.indefinite: bool = False

  @property
  def is_empty(self) -> bool:
    return not (self.values or self.indefinite)

  def __repr__(self):
    indef = "*" if self.indefinite else ""
    return f"<Option: {self.typ}: {self.values}{indef}>"


class _OptionSet:
  """Holds a set of options."""

  def __init__(self):
    # Collection of options, stored as a dict rather than a set so we can find a
    # given option efficiently.
    self._options: dict[abstract.Class, _Option] = {}

  def __iter__(self):
    yield from self._options.values()

  def __bool__(self):
    return not self.is_complete

  @property
  def is_complete(self) -> bool:
    return all(x.is_empty for x in self)

  def add_instance(self, val):
    """Add an instance to the match options."""
    cls = val.cls
    if cls not in self._options:
      self._options[cls] = _Option(cls)
    if isinstance(val, abstract.ConcreteValue):
      self._options[cls].values.add(val)
    else:
      self.add_type(cls)

  def add_type(self, cls):
    """Add an class to the match options."""
    if cls not in self._options:
      self._options[cls] = _Option(cls)
    vals = _get_class_values(cls)
    if vals is not None:
      self._options[cls].values.update(vals)
    else:
      self._options[cls].indefinite = True

  def cover_instance(self, val) -> list[_Value]:
    """Remove an instance from the match options."""
    assert isinstance(val, abstract.Instance)
    cls = val.cls
    if cls not in self._options:
      return []
    opt = self._options[cls]
    if cls.is_enum:
      val = val.name
    if val in opt.values:
      opt.values.remove(val)
      return [val]
    else:
      if (
          not cls.is_enum
          and not isinstance(val, abstract.ConcreteValue)
          and opt.values
      ):
        # We have passed in an indefinite value to a match var with concrete
        # values; we can no longer be sure which values of the type are covered.
        opt.indefinite = True
      return [val] if opt.indefinite else []

  def cover_type(self, val) -> list[_Value]:
    """Remove a class and any associated instances from the match options."""
    if val not in self._options:
      return []
    opt = self._options[val]
    vals = list(opt.values)
    opt.values = set()
    if opt.indefinite:
      # opt is now empty; we have covered all potential values
      opt.indefinite = False
      return [val]
    else:
      return vals


class _OptionTracker:
  """Tracks a set of match options."""

  def __init__(self, match_var, ctx):
    self.match_var: cfg.Variable = match_var
    self.ctx = ctx
    self.options: _OptionSet = _OptionSet()
    self.could_contain_anything: bool = False
    # The types of the match var within each case branch
    self.cases: dict[int, _OptionSet] = collections.defaultdict(_OptionSet)
    self.is_valid: bool = True

    for d in match_var.data:
      if isinstance(d, abstract.Unsolvable):
        self.is_valid = False
        self.could_contain_anything = True
      elif isinstance(d, abstract.Instance):
        self.options.add_instance(d)
      else:
        self.options.add_type(d)

  @property
  def is_complete(self) -> bool:
    return self.options.is_complete

  def get_narrowed_match_var(self, node) -> cfg.Variable:
    if self.could_contain_anything:
      return self.match_var.AssignToNewVariable(node)
    else:
      narrowed = []
      for opt in self.options:
        if not opt.is_empty:
          narrowed.append(opt.typ.instantiate(node))
      return self.ctx.join_variables(node, narrowed)

  def cover(self, line, var) -> list[_Value]:
    ret = []
    for d in var.data:
      if isinstance(d, abstract.Instance):
        ret += self.options.cover_instance(d)
        self.cases[line].add_instance(d)
      else:
        ret += self.options.cover_type(d)
        self.cases[line].add_type(d)
    return ret

  def cover_from_cmp(self, line, case_var) -> list[_Value]:
    """Cover cases based on a CMP match."""
    ret = []
    # If we compare `match_var == constant`, add the type of `constant` to the
    # current case so that instantiate_case_var can retrieve it.
    for d in case_var.data:
      if isinstance(d, abstract.Unsolvable):
        # Set the case type to Any and invalidate the tracker; we do not know
        # what we have matched against.
        ret += self.options.cover_type(d)
        self.invalidate()
      elif isinstance(d, abstract.Instance):
        ret += self.options.cover_instance(d)
        self.cases[line].add_instance(d)
        if isinstance(d, abstract.ConcreteValue) and d.pyval is None:
          # Need to special-case `case None` since it's compiled differently.
          ret += self.options.cover_type(d.cls)
      else:
        # We do not handle whatever case this is; just invalidate the tracker
        # TODO(mdemello): This is probably an error in the user's code; we
        # should figure out a way to report it.
        self.invalidate()
    return ret

  def cover_from_none(self, line) -> list[_Value]:
    cls = self.ctx.convert.none_type
    self.cases[line].add_type(cls)
    return self.options.cover_type(cls)

  def invalidate(self):
    self.is_valid = False


class _MatchTypes(enum.Enum):
  """Track match types based on generated opcode."""

  CLASS = enum.auto()
  SEQUENCE = enum.auto()
  KEYS = enum.auto()
  MAPPING = enum.auto()
  CMP = enum.auto()

  @classmethod
  def make(cls, op: opcodes.Opcode):
    if op.name.startswith("MATCH_"):
      return cls[op.name[len("MATCH_") :]]
    else:
      return cls.CMP


class _Matches:
  """Tracks branches of match statements."""

  def __init__(self, ast_matches):
    self.start_to_end = {}  # match_line : match_end_line
    self.end_to_starts = collections.defaultdict(list)
    self.match_cases = {}  # opcode_line : match_line
    self.defaults = set()  # lines with defaults
    self.as_names = {}  # case_end_line : case_as_name
    self.unseen_cases = {}  # match_line : {unseen_cases}

    for m in ast_matches.matches:
      self._add_match(m.start, m.end, m.cases)

  def _add_match(self, start, end, cases):
    self.start_to_end[start] = end
    self.end_to_starts[end].append(start)
    self.unseen_cases[start] = {c.start for c in cases}
    for c in cases:
      for i in range(c.start, c.end + 1):
        self.match_cases[i] = start
      if c.is_underscore:
        self.defaults.add(c.start)
      if c.as_name:
        self.as_names[c.end] = c.as_name

  def register_case(self, match_line, case_line):
    assert self.match_cases[case_line] == match_line
    self.unseen_cases[match_line].discard(case_line)

  def __repr__(self):
    return f"""
      Matches: {sorted(self.start_to_end.items())}
      Cases: {self.match_cases}
      Defaults: {self.defaults}
    """


@dataclasses.dataclass
class IncompleteMatch:
  """A list of uncovered cases, for error reporting."""

  line: int
  cases: set[str]


class BranchTracker:
  """Track exhaustiveness in pattern matches."""

  def __init__(self, ast_matches, ctx):
    self.matches = _Matches(ast_matches)
    self._option_tracker: dict[int, dict[int, _OptionTracker]] = (
        collections.defaultdict(dict)
    )
    self._match_types: dict[int, set[_MatchTypes]] = collections.defaultdict(
        set
    )
    self._active_ends = set()
    self.ctx = ctx

  def _get_option_tracker(
      self, match_var: cfg.Variable, match_line: int
  ) -> _OptionTracker:
    """Get the option tracker for a match line."""
    if (
        match_line not in self._option_tracker
        or match_var.id not in self._option_tracker[match_line]
    ):
      self._option_tracker[match_line][match_var.id] = _OptionTracker(
          match_var, self.ctx
      )
      self._active_ends.add(self.matches.start_to_end[match_line])
    return self._option_tracker[match_line][match_var.id]

  def _make_instance_for_match(self, node, types):
    """Instantiate a type for match case narrowing."""
    # This specifically handles the case where we match against an
    # AnnotationContainer in MATCH_CLASS, and need to replace it with its base
    # class when narrowing the matched variable.
    ret = []
    for v in types:
      cls = v.base_cls if isinstance(v, abstract.AnnotationContainer) else v
      if not isinstance(cls, (abstract.Class, abstract.AMBIGUOUS)):
        self.ctx.errorlog.bad_class_match(self.ctx.vm.frames, cls)
        return self.ctx.new_unsolvable(node)
      ret.append(self.ctx.vm.init_class(node, cls))
    return self.ctx.join_variables(node, ret)

  def _register_case_branch(self, op: opcodes.Opcode) -> int | None:
    match_line = self.matches.match_cases.get(op.line)
    if match_line is None:
      return None
    self.matches.register_case(match_line, op.line)
    return match_line

  def instantiate_case_var(self, op, match_var, node):
    match_line = self.matches.match_cases[op.line]
    tracker = self._get_option_tracker(match_var, match_line)
    if tracker.cases[op.line]:
      # We have matched on one or more classes in this case.
      types = [x.typ for x in tracker.cases[op.line]]
      return self._make_instance_for_match(node, types)
    else:
      # We have not matched on a type, just bound the current match var to a
      # variable.
      return tracker.get_narrowed_match_var(node)

  def get_current_type_tracker(
      self, op: opcodes.Opcode, match_var: cfg.Variable
  ):
    line = self.get_current_match(op)
    return self._option_tracker[line].get(match_var.id)

  def get_current_type_trackers(self, op: opcodes.Opcode):
    line = self.get_current_match(op)
    return list(self._option_tracker[line].values())

  def get_current_match(self, op: opcodes.Opcode):
    match_line = self.matches.match_cases[op.line]
    return match_line

  def is_current_as_name(self, op: opcodes.Opcode, name: str):
    if op.line not in self.matches.match_cases:
      return None
    return self.matches.as_names.get(op.line) == name

  def register_match_type(self, op: opcodes.Opcode):
    if op.line not in self.matches.match_cases:
      return
    match_line = self.matches.match_cases[op.line]
    self._match_types[match_line].add(_MatchTypes.make(op))

  def add_none_branch(self, op: opcodes.Opcode, match_var: cfg.Variable):
    match_line = self._register_case_branch(op)
    if not match_line:
      return None
    tracker = self._get_option_tracker(match_var, match_line)
    tracker.cover_from_none(op.line)
    if not tracker.is_complete:
      return None
    else:
      # This is the last remaining case, and will always succeed.
      return True

  def add_cmp_branch(
      self,
      op: opcodes.OpcodeWithArg,
      cmp_type: int,
      match_var: cfg.Variable,
      case_var: cfg.Variable,
  ) -> _MatchSuccessType:
    """Add a compare-based match case branch to the tracker."""
    match_line = self._register_case_branch(op)
    if not match_line:
      return None

    if cmp_type not in (slots.CMP_EQ, slots.CMP_IS):
      return None

    match_type = self._match_types[match_line]

    try:
      case_val = abstract_utils.get_atomic_value(case_var)
    except abstract_utils.ConversionError:
      return None

    # If this is part of a case statement and the match includes class matching,
    # check if we need to include the compared value as a type case.
    # (We need to do this whether or not the match_var has a concrete value
    # because even an ambigious cmp match will require the type to be set within
    # the case branch).
    op = cast(opcodes.OpcodeWithArg, op)
    if op.line not in self.matches.match_cases:
      return None
    tracker = self.get_current_type_tracker(op, match_var)
    # If we are not part of a class match, check if we have an exhaustive match
    # (enum or union of literals) that we are tracking.
    if not tracker:
      if _is_literal_match(match_var) or _is_enum_match(match_var, case_val):
        tracker = self._get_option_tracker(match_var, match_line)

    # If none of the above apply we cannot do any sort of tracking.
    if not tracker:
      return None

    ret = tracker.cover_from_cmp(op.line, case_var)
    if match_type != {_MatchTypes.CMP}:
      # We only do exhaustiveness tracking for pure CMP matches
      tracker.invalidate()
      return None
    elif tracker.is_complete:
      # This is the last remaining case, and will always succeed.
      return True
    elif ret:
      return None
    else:
      return False

  def add_class_branch(
      self, op: opcodes.Opcode, match_var: cfg.Variable, case_var: cfg.Variable
  ) -> _MatchSuccessType:
    """Add a class-based match case branch to the tracker."""
    match_line = self._register_case_branch(op)
    if not match_line:
      return None
    tracker = self._get_option_tracker(match_var, match_line)
    tracker.cover(op.line, case_var)
    return tracker.is_complete or None

  def add_default_branch(self, op: opcodes.Opcode) -> _MatchSuccessType:
    """Add a default match case branch to the tracker."""
    match_line = self._register_case_branch(op)
    if not match_line or match_line not in self._option_tracker:
      return None

    for opt in self._option_tracker[match_line].values():
      # We no longer check for exhaustive or redundant matches once we hit a
      # default case.
      opt.invalidate()
    return True

  def check_ending(
      self, op: opcodes.Opcode, implicit_return: bool = False
  ) -> list[IncompleteMatch]:
    """Check if we have ended a match statement with leftover cases."""
    line = op.line
    if implicit_return:
      done = set()
      if line in self.matches.match_cases:
        start = self.matches.match_cases[line]
        end = self.matches.start_to_end[start]
        if end in self._active_ends:
          done.add(end)
    else:
      done = {i for i in self._active_ends if line > i}
    ret = []
    for i in done:
      for start in self.matches.end_to_starts[i]:
        if self.matches.unseen_cases[start]:
          # We have executed some opcode out of order and thus gone past the end
          # of the match block before seeing all case branches.
          continue
        trackers = self._option_tracker[start]
        for tracker in trackers.values():
          if tracker.is_valid:
            for o in tracker.options:
              if not o.is_empty and not o.indefinite:
                ret.append(IncompleteMatch(start, o.values))
    self._active_ends -= done
    return ret
