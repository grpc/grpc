"""Parse a pyi file using the ast library."""

import ast as astlib
import dataclasses
import hashlib
import io
import keyword
import re
import sys
import tokenize
from typing import Any, cast

from pytype.ast import debug
from pytype.pyi import conditions
from pytype.pyi import definitions
from pytype.pyi import evaluator
from pytype.pyi import function
from pytype.pyi import modules
from pytype.pyi import types
from pytype.pyi import visitor
from pytype.pytd import pep484
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import visitors
from pytype.pytd.codegen import decorate

# reexport as parser.ParseError
ParseError = types.ParseError

# ------------------------------------------------------
# imports

_UNKNOWN_IMPORT = "__unknown_import__"


def _import_from_module(module: str | None, level: int) -> str:
  """Convert a typedast import's 'from' into one that add_import expects."""
  if module is None:
    return {1: "__PACKAGE__", 2: "__PARENT__"}[level]
  prefix = "." * level
  return prefix + module


def _keyword_to_parseable_name(kw):
  return f"__KW_{kw}__"


def _parseable_name_to_real_name(name):
  m = re.fullmatch(r"__KW_(?P<keyword>.+)__", name)
  return m.group("keyword") if m else name


# ------------------------------------------------------
# typevars


@dataclasses.dataclass
class _TypeVariable:
  """Internal representation of type variables."""

  kind: str  # TypeVar or ParamSpec
  name: str
  bound: pytd.Type | None
  constraints: list[pytd.Type]
  default: pytd.Type | list[pytd.Type] | None

  @classmethod
  def from_call(cls, kind: str, node: astlib.Call):
    """Construct a TypeVar or ParamSpec from an ast.Call node."""
    if not node.args:
      raise ParseError(f"Missing arguments to {kind}")
    name = cast(types.Pyval, node.args[0])
    constraints = cast(list[pytd.Type], node.args[1:])
    if not types.Pyval.is_str(name):
      raise ParseError(f"Bad arguments to {kind}")
    bound = None
    default = None
    # TODO(rechen): We should enforce the PEP 484 guideline that
    # len(constraints) != 1. However, this guideline is currently violated
    # in typeshed (see https://github.com/python/typeshed/pull/806).
    kws = {x.arg for x in node.keywords}
    extra = kws - {"bound", "covariant", "contravariant", "default"}
    if extra:
      raise ParseError(f"Unrecognized keyword(s): {', '.join(extra)}")
    for kw in node.keywords:
      if kw.arg == "bound":
        bound = kw.value
      elif kw.arg == "default":
        default = kw.value
    return cls(kind, name.value, bound, constraints, default)


# ------------------------------------------------------
# Main tree visitor and generator code


def _attribute_to_name(node: astlib.Attribute) -> astlib.Name:
  """Recursively convert Attributes to Names."""
  val = node.value
  if isinstance(val, astlib.Name):
    prefix = val.id
  elif isinstance(val, astlib.Attribute):
    prefix = _attribute_to_name(val).id
  elif isinstance(val, (pytd.NamedType, pytd.Module)):
    prefix = val.name
  else:
    msg = f"Unexpected attribute access on {val!r} [{type(val)}]"
    raise ParseError(msg)
  return astlib.Name(f"{prefix}.{node.attr}")


def _read_str_list(name, value):
  if not (
      isinstance(value, (list, tuple))
      and all(types.Pyval.is_str(x) for x in value)
  ):
    raise ParseError(f"{name} must be a list of strings")
  return tuple(x.value for x in value)


class _ConvertConstantsVisitor(visitor.BaseVisitor):
  """Converts ast module constants to our own representation."""

  def __init__(self, filename):
    super().__init__(filename=filename, visit_decorators=True)

  def visit_Constant(self, node):
    if node.value is Ellipsis:
      return definitions.Definitions.ELLIPSIS
    return types.Pyval.from_const(node)

  def visit_UnaryOp(self, node):
    if isinstance(node.op, astlib.USub):
      if isinstance(node.operand, types.Pyval):
        return node.operand.negated()
    raise ParseError(f"Unexpected unary operator: {node.op}")

  def visit_Assign(self, node):
    if node.type_comment:
      # Convert the type comment from a raw string to a string constant.
      node.type_comment = types.Pyval(
          "str", node.type_comment, *types.node_position(node)
      )


class _AnnotationVisitor(visitor.BaseVisitor):
  """Converts ast type annotations to pytd."""

  def __init__(self, filename, defs):
    super().__init__(filename=filename)
    self.defs = defs
    self.subscripted = []  # Keep track of the name being subscripted.

  def show(self, node):
    print(debug.dump(node, astlib, include_attributes=False))

  def _convert_late_annotation(self, annotation):
    # Late annotations may need to be parsed into an AST first
    if annotation.isalpha():
      return self.defs.new_type(annotation)
    try:
      a = astlib.parse(annotation)
      # Unwrap the module the parser puts around the source string
      typ = cast(list[astlib.Expr], a.body)[0].value
      return self.visit(_ConvertConstantsVisitor(self.filename).visit(typ))
    except ParseError as e:
      # Clear out position information since it is relative to the typecomment
      e.clear_position()
      raise e

  def _in_literal(self):
    if not self.subscripted:
      return False
    last = self.subscripted[-1]
    if isinstance(last, astlib.Name):
      return self.defs.matches_type(last.id, "typing.Literal")
    return False

  def visit_Pyval(self, node):
    # Handle a types.Pyval node (converted from a literal constant).
    if node.type == "NoneType":
      return pytd.NamedType("NoneType")
    elif self._in_literal():
      return node
    elif node.type == "str" and node.value:
      return self._convert_late_annotation(node.value)
    else:
      raise ParseError(f"Unexpected literal: {node.value!r}")

  def visit_Tuple(self, node):
    return tuple(node.elts)

  def visit_List(self, node):
    return list(node.elts)

  def visit_Name(self, node):
    return self.defs.new_type(node.id)

  def _convert_getattr(self, node):
    # The protobuf pyi generator outputs getattr(X, 'attr') when 'attr' is a
    # Python keyword.
    if node.func.name != "getattr" or len(node.args) != 2:
      return None
    obj, attr = node.args
    if not all(isinstance(t, pytd.NamedType) for t in (obj, attr)):
      return None
    return pytd.NamedType(f"{obj.name}.{attr.name}")

  def visit_Call(self, node):
    ret = self._convert_getattr(node)
    if ret:
      return ret
    raise ParseError(
        "Constructors and function calls in type annotations are not supported."
    )

  def _get_subscript_params(self, node):
    return node.slice

  def _set_subscript_params(self, node, new_val):
    node.slice = new_val

  def _convert_typing_annotated_args(self, node):
    typ, *args = self._get_subscript_params(node).elts
    typ = self.visit(typ)
    params = (_MetadataVisitor().visit(x) for x in args)
    self._set_subscript_params(node, (typ,) + tuple(params))

  def enter_Subscript(self, node):
    if isinstance(node.value, astlib.Attribute):
      value = _attribute_to_name(node.value)
    else:
      value = node.value
    self.subscripted.append(value)
    if not isinstance(value, astlib.Name):
      return
    # This is needed because
    #   Foo[X]
    # parses to
    #   Subscript(Name(id = Foo), Name(id = X))
    # so we would see visit_Name(Foo) before visit_Subscript(Foo[X]). We would
    # assume that we are seing a bare Foo, and possibly emit an error about Foo
    # needing to be parameterized. So we simply resolve the name here, and
    # create a new type when we get the param list in visit_Subscript.
    node.value = self.defs.resolve_type(value.id)
    # We process typing.Annotated metadata early so that the metadata is not
    # mistaken for a bad type.
    if self.defs.matches_type(value.id, "typing.Annotated"):
      self._convert_typing_annotated_args(node)

  def visit_Subscript(self, node):
    params = self._get_subscript_params(node)
    if type(params) is not tuple:  # pylint: disable=unidiomatic-typecheck
      params = (params,)
    return self.defs.new_type(node.value, params)

  def leave_Subscript(self, node):
    self.subscripted.pop()

  def visit_Attribute(self, node):
    annotation = _attribute_to_name(node).id
    return self.defs.new_type(annotation)

  def visit_BinOp(self, node):
    if self._in_literal():
      raise ParseError("Expressions are not allowed in typing.Literal.")
    elif isinstance(node.op, astlib.BitOr):
      return self.defs.new_type("typing.Union", [node.left, node.right])
    else:
      raise ParseError(f"Unexpected operator {node.op}")

  def visit_BoolOp(self, node):
    if isinstance(node.op, astlib.Or):
      raise ParseError("Deprecated syntax `x or y`; use `Union[x, y]` instead")
    else:
      raise ParseError(f"Unexpected operator {node.op}")


class _MetadataVisitor(visitor.BaseVisitor):
  """Converts typing.Annotated metadata."""

  def visit_Call(self, node):
    posargs = tuple(evaluator.literal_eval(x) for x in node.args)
    kwargs = {x.arg: evaluator.literal_eval(x.value) for x in node.keywords}
    if isinstance(node.func, astlib.Attribute):
      func_name = _attribute_to_name(node.func)
    else:
      func_name = node.func
    return (func_name.id, posargs, kwargs)

  def visit_Dict(self, node):
    return evaluator.literal_eval(node)


def _flatten_splices(body: list[Any]) -> list[Any]:
  """Flatten a list with nested Splices."""
  if not any(isinstance(x, Splice) for x in body):
    return body
  out = []
  for x in body:
    if isinstance(x, Splice):
      # This technically needn't be recursive because of how we build Splices
      # but better not to have the class assume that.
      out.extend(_flatten_splices(x.body))
    else:
      out.append(x)
  return out


class Splice:
  """Splice a list into a node body."""

  def __init__(self, body):
    self.body = _flatten_splices(body)

  def __str__(self):
    return "Splice(\n" + ",\n  ".join([str(x) for x in self.body]) + "\n)"

  def __repr__(self):
    return str(self)


class _GeneratePytdVisitor(visitor.BaseVisitor):
  """Converts an ast tree to a pytd tree."""

  _NOOP_NODES = {
      # Expression contexts are ignored.
      astlib.Load,
      astlib.Store,
      astlib.Del,
      # Appears as an operator in `__all__ += ...`.
      astlib.Add,
      # These nodes are passed through unchanged and processed by their parents.
      astlib.arg,
      astlib.arguments,
      astlib.keyword,
      types.Pyval,
  }

  _ANNOT_NODES = (
      astlib.Attribute,
      astlib.BinOp,
      astlib.Name,
      astlib.Subscript,
  )

  def __init__(self, src, filename, module_name, options):
    super().__init__(filename=filename, src_code=src, visit_decorators=True)
    defs = definitions.Definitions(modules.Module(filename, module_name))
    self.defs = defs
    self.module_name = module_name
    self.options = options
    self.level = 0
    self.in_function = False  # pyi will not have nested defs
    self.annotation_visitor = _AnnotationVisitor(filename=filename, defs=defs)
    self.class_stack = []

  def show(self, node):
    print(debug.dump(node, astlib, include_attributes=True))

  def generic_visit(self, node):
    node_type = type(node)
    if node_type in self._NOOP_NODES:
      return node
    raise NotImplementedError(f"Unsupported node type: {node_type.__name__}")

  def visit_Module(self, node):
    node.body = _flatten_splices(node.body)
    return self.defs.build_type_decl_unit(node.body)

  def visit_Pass(self, node):
    return self.defs.ELLIPSIS

  def visit_Expr(self, node):
    # Handle some special cases of expressions that can occur in class and
    # module bodies.
    if node.value == self.defs.ELLIPSIS:
      # class x: ...
      return node.value
    elif types.Pyval.is_str(node.value):
      # docstrings
      return Splice([])
    else:
      raise ParseError(f"Unexpected expression: {node.value}")

  def _extract_function_properties(self, node):
    decorators = []
    abstract = coroutine = final = overload = False
    for d in node.decorator_list:
      # Since we can't import other parts of the stdlib in builtins and typing,
      # we treat the abstractmethod and coroutine decorators as pseudo-builtins.
      if self.defs.matches_type(
          d.name, ("builtins.abstractmethod", "abc.abstractmethod")
      ):
        abstract = True
      elif self.defs.matches_type(
          d.name,
          ("builtins.coroutine", "asyncio.coroutine", "coroutines.coroutine"),
      ):
        coroutine = True
      elif self.defs.matches_type(d.name, "typing.final"):
        final = True
      elif self.defs.matches_type(d.name, "typing.overload"):
        overload = True
      else:
        decorators.append(d)
    return decorators, function.SigProperties(
        abstract=abstract,
        coroutine=coroutine,
        final=final,
        overload=overload,
        is_async=isinstance(node, astlib.AsyncFunctionDef),
    )

  def visit_FunctionDef(self, node):
    node.decorator_list, props = self._extract_function_properties(node)
    node.body = _flatten_splices(node.body)
    return function.NameAndSig.from_function(node, props)

  def visit_AsyncFunctionDef(self, node):
    return self.visit_FunctionDef(node)

  def visit_AnnAssign(self, node):
    return self._ann_assign(node.target, node.annotation, node.value)

  def _ann_assign(self, name, typ, val):
    is_alias = False
    if name == "__match_args__" and isinstance(val, tuple):
      typ = pytd.NamedType("tuple")
      val = None
    elif typ.name:
      if self.defs.matches_type(typ.name, "typing.Final"):
        if isinstance(val, types.Pyval):
          # to_pytd_literal raises an exception if the value is a float, but
          # checking upfront allows us to generate a nicer error message.
          if isinstance(val.value, float):
            raise ParseError(
                f"Default value for {name}: Final can only be '...' or a legal "
                f"Literal parameter, got {val}"
            )
          else:
            typ = val.to_pytd_literal()
            val = None
        elif isinstance(val, pytd.NamedType):
          typ = pytd.Literal(val)
          val = None
      elif self.defs.matches_type(typ.name, "typing.TypeAlias"):
        if not val:
          raise ParseError(f"Missing default value for {name}: {typ.name}")
        typ = self.defs.new_type_from_value(val) or val
        val = None
        is_alias = True
      elif (
          self.module_name == "typing_extensions" and typ.name == "_SpecialForm"
      ):

        def type_of(n):
          return pytd.GenericType(
              pytd.NamedType("builtins.type"), (pytd.NamedType(n),)
          )

        # We convert known special forms to their corresponding types and
        # otherwise treat them as unknown types.
        if name in {"Final", "Protocol", "Self", "TypeGuard", "TypeIs"}:
          typ = type_of(f"typing.{name}")
        elif name == "LiteralString":
          # TODO(b/303083512): Support LiteralString.
          typ = type_of("builtins.str")
        else:
          typ = pytd.AnythingType()
    if val:
      if isinstance(val, (types.Ellipsis, types.Pyval)):
        val = pytd.AnythingType()
      else:
        raise ParseError(
            f"Default value for {name}: {typ.name} can only be '...' or a "
            f"literal constant, got {val}"
        )
    if is_alias:
      assert not val
      ret = pytd.Alias(name, typ)
    else:
      ret = pytd.Constant(name, typ, val)
    if self.level == 0:
      self.defs.add_alias_or_constant(ret)
    return ret

  def visit_AugAssign(self, node):
    if node.target == "__all__":
      # Ignore other assignments
      self.defs.all += _read_str_list(node.target, node.value)
    return Splice([])

  def _bare_assign(self, name, typ, val):
    if typ:
      if val is self.defs.ELLIPSIS:
        # `name = ...  # type: typ` converts to `name: typ`, dropping `...`.
        return self._ann_assign(name, typ, None)
      else:
        return self._ann_assign(name, typ, val)

    # Record and erase TypeVar and ParamSpec definitions.
    if isinstance(val, _TypeVariable):
      self.defs.add_type_variable(name, val)
      return Splice([])

    if getattr(val, "name", None) == _UNKNOWN_IMPORT:
      constant = pytd.Constant(name, pytd.AnythingType())
      self.defs.add_alias_or_constant(constant)
      return constant

    if name == "__slots__":
      if self.level == 0:
        raise ParseError("__slots__ only allowed on the class level")
      return types.SlotDecl(_read_str_list(name, val))

    if name == "__all__" and isinstance(val, (list, tuple)):
      self.defs.all = _read_str_list(name, val)
      return Splice([])

    ret = self.defs.new_alias_or_constant(name, val)
    if self.in_function:
      return function.Mutator(name, ret.type)
    if self.level == 0:
      self.defs.add_alias_or_constant(ret)
    return ret

  def visit_Assign(self, node):
    out = []
    value = node.value
    for target in node.targets:
      if isinstance(target, tuple):
        count = len(target)
        if not (isinstance(value, tuple) and count == len(value)):
          msg = f"Cannot unpack {count} values for multiple assignment"
          raise ParseError(msg)
        for k, v in zip(target, value):
          out.append(self._bare_assign(k, node.type_comment, v))
      else:
        out.append(self._bare_assign(target, node.type_comment, value))
    return Splice(out)

  def visit_ClassDef(self, node):
    full_class_name = ".".join(self.class_stack)
    self.defs.type_map[full_class_name] = pytd.NamedType(full_class_name)
    defs = _flatten_splices(node.body)
    return self.defs.build_class(
        full_class_name, node.bases, node.keywords, node.decorator_list, defs
    )

  def enter_If(self, node):
    # Evaluate the test and preemptively remove the invalid branch so we don't
    # waste time traversing it.
    node.test = conditions.evaluate(node.test, self.options)
    if not isinstance(node.test, bool):
      raise ParseError("Unexpected if statement " + debug.dump(node, astlib))

    if node.test:
      node.orelse = []
    else:
      node.body = []

  def visit_If(self, node):
    if not isinstance(node.test, bool):
      raise ParseError("Unexpected if statement " + debug.dump(node, astlib))

    if node.test:
      return Splice(node.body)
    else:
      return Splice(node.orelse)

  def visit_Import(self, node):
    if self.level > 0:
      raise ParseError("Import statements need to be at module level")
    self.defs.add_import(None, node.names)
    return Splice([])

  def visit_ImportFrom(self, node):
    if self.level > 0:
      raise ParseError("Import statements need to be at module level")
    module = _import_from_module(node.module, node.level)
    self.defs.add_import(module, node.names)
    return Splice([])

  def visit_alias(self, node):
    if node.asname is None:
      return node.name
    return node.name, node.asname

  def visit_Name(self, node):
    return _parseable_name_to_real_name(node.id)

  def visit_Attribute(self, node):
    return f"{node.value}.{node.attr}"

  def visit_Tuple(self, node):
    return tuple(node.elts)

  def visit_List(self, node):
    return list(node.elts)

  def visit_Dict(self, node):
    return dict(zip(node.keys, node.values))

  def visit_Call(self, node):
    func = node.func.name or ""
    for tvar_kind in ("TypeVar", "ParamSpec"):
      if self.defs.matches_type(func, f"typing.{tvar_kind}"):
        if self.level > 0:
          raise ParseError(f"{tvar_kind}s need to be defined at module level")
        return _TypeVariable.from_call(tvar_kind, node)
    if self.defs.matches_type(func, "typing.NamedTuple"):
      if len(node.args) != 2:
        msg = "Wrong args: expected NamedTuple(name, [(field, type), ...])"
        raise ParseError(msg)
      name, fields = node.args
      return self.defs.new_named_tuple(
          name.value, [(n.value, t) for n, t in fields]
      )
    elif self.defs.matches_type(func, "collections.namedtuple"):
      if len(node.args) != 2:
        msg = "Wrong args: expected namedtuple(name, [field, ...])"
        raise ParseError(msg)
      name, fields = node.args
      typed_fields = [(n.value, pytd.AnythingType()) for n in fields]
      return self.defs.new_named_tuple(name.value, typed_fields)
    elif self.defs.matches_type(func, "typing.TypedDict"):
      if len(node.args) != 2:
        msg = "Wrong args: expected TypedDict(name, {field: type, ...})"
        raise ParseError(msg)
      name, fields = node.args
      return self.defs.new_typed_dict(
          name.value, {n.value: t for n, t in fields.items()}, node.keywords
      )
    elif self.defs.matches_type(func, "typing.NewType"):
      if len(node.args) != 2:
        msg = "Wrong args: expected NewType(name, type)"
        raise ParseError(msg)
      name, typ = node.args
      return self.defs.new_new_type(name.value, typ)
    elif self.defs.matches_type(func, "importlib.import_module"):
      if self.level > 0:
        raise ParseError("Import statements need to be at module level")
      return pytd.NamedType(_UNKNOWN_IMPORT)
    # Convert all other calls to their function names; for example, typing.pyi
    # uses things like:
    #     List = _Alias()
    return node.func

  def visit_Raise(self, node):
    return types.Raise(node.exc)

  # We convert type comments and annotations in enter() because we want to
  # convert an entire type at once rather than bottom-up.  enter() and leave()
  # are also used to track nesting level.

  def _convert_value(self, node):
    if isinstance(node.value, self._ANNOT_NODES):
      node.value = self.annotation_visitor.visit(node.value)
    elif isinstance(node.value, (astlib.Tuple, astlib.List)):
      elts = [
          self.annotation_visitor.visit(x)
          if isinstance(x, self._ANNOT_NODES)
          else x
          for x in node.value.elts
      ]
      node.value = type(node.value)(elts)

  def enter_Assign(self, node):
    if node.type_comment:
      node.type_comment = self.annotation_visitor.visit(node.type_comment)
    self._convert_value(node)

  def enter_AnnAssign(self, node):
    if node.annotation:
      node.annotation = self.annotation_visitor.visit(node.annotation)
    self._convert_value(node)

  def enter_arg(self, node):
    if node.annotation:
      node.annotation = self.annotation_visitor.visit(node.annotation)

  def _convert_list(self, lst, start=0):
    lst[start:] = [self.annotation_visitor.visit(x) for x in lst[start:]]

  def _convert_newtype_args(self, node: astlib.Call):
    self._convert_list(node.args, start=1)

  def _convert_typing_namedtuple_args(self, node: astlib.Call):
    for fields in node.args[1:]:
      for field in cast(astlib.List, fields).elts:
        self._convert_list(cast(astlib.Tuple, field).elts, start=1)

  def _convert_typevar_args(self, node: astlib.Call):
    self._convert_list(node.args, start=1)
    for kw in node.keywords:
      if kw.arg == "bound":
        kw.value = self.annotation_visitor.visit(kw.value)
      elif kw.arg == "default":
        kw.value = self.annotation_visitor.visit(kw.value)

  def _convert_typed_dict_args(self, node: astlib.Call):
    for fields in node.args[1:]:
      self._convert_list(cast(astlib.Dict, fields).values)

  def enter_Call(self, node):
    node.func = self.annotation_visitor.visit(node.func)
    func = node.func.name or ""
    if self.defs.matches_type(
        func, ("typing.TypeVar", "typing.ParamSpec", "typing.TypeVarTuple")
    ):
      self._convert_typevar_args(node)
    elif self.defs.matches_type(func, "typing.NamedTuple"):
      self._convert_typing_namedtuple_args(node)
    elif self.defs.matches_type(func, "typing.TypedDict"):
      self._convert_typed_dict_args(node)
    elif self.defs.matches_type(func, "typing.NewType"):
      return self._convert_newtype_args(node)

  def enter_Raise(self, node):
    exc = node.exc.func if isinstance(node.exc, astlib.Call) else node.exc
    node.exc = self.annotation_visitor.visit(exc)

  def _convert_decorators(self, node):
    decorators = []
    for d in node.decorator_list:
      base = d.func if isinstance(d, astlib.Call) else d
      if isinstance(base, astlib.Attribute):
        name = _attribute_to_name(base)
      else:
        name = base
      typ = self.annotation_visitor.visit(name)
      # Wrap as aliases so that we can reference functions as types.
      decorators.append(pytd.Alias(name.id, typ))
    node.decorator_list = decorators

  def enter_FunctionDef(self, node):
    self._convert_decorators(node)
    if node.returns:
      node.returns = self.annotation_visitor.visit(node.returns)
    self.level += 1
    self.in_function = True

  def leave_FunctionDef(self, node):
    self.level -= 1
    self.in_function = False

  def enter_AsyncFunctionDef(self, node):
    self.enter_FunctionDef(node)

  def leave_AsyncFunctionDef(self, node):
    self.leave_FunctionDef(node)

  def enter_ClassDef(self, node):
    self._convert_decorators(node)
    node.bases = [
        self.annotation_visitor.visit(base)
        if isinstance(base, self._ANNOT_NODES)
        else base
        for base in node.bases
    ]
    for kw in node.keywords:
      if kw.arg == "metaclass":
        kw.value = self.annotation_visitor.visit(kw.value)
    self.level += 1
    self.class_stack.append(_parseable_name_to_real_name(node.name))

  def leave_ClassDef(self, node):
    self.level -= 1
    self.class_stack.pop()


def post_process_ast(ast, src, name=None):
  """Post-process the parsed AST."""
  ast = definitions.finalize_ast(ast)
  ast = ast.Visit(pep484.ConvertTypingToNative(name))

  if name:
    ast = ast.Replace(name=name)
    ast = ast.Visit(visitors.ResolveLocalNames())
  else:
    # If there's no unique name, hash the sourcecode.
    ast = ast.Replace(name=hashlib.md5(src.encode("utf-8")).hexdigest())
  ast = ast.Visit(visitors.StripExternalNamePrefix())

  # Now that we have resolved external names, validate any class decorators that
  # do code generation. (We will generate the class lazily, but we should check
  # for errors at parse time so they can be reported early.)
  try:
    ast = ast.Visit(decorate.ValidateDecoratedClassVisitor())
  except TypeError as e:
    # Convert errors into ParseError. Unfortunately we no longer have location
    # information if an error is raised during transformation of a class node.
    raise ParseError.from_exc(e)

  return ast


def _fix_src(src: str) -> str:
  """Attempts to fix syntax errors in the source code."""
  # TODO(b/294445640): This is a hacky workaround to deal with invalid stubs
  # produced by the protobuf pyi generator.
  try:
    tokens = list(tokenize.generate_tokens(io.StringIO(src).readline))
  except SyntaxError:
    return src
  num_tokens = len(tokens)

  def _is_classname(i):
    return i and tokens[i - 1].string == "class"

  def _is_varname(i):
    if i and tokens[i - 1].string.strip():  # not proceeded by whitespace
      return False
    return i < num_tokens - 1 and tokens[i + 1].type == tokenize.OP

  lines = src.splitlines()
  for i, token in enumerate(tokens):
    if (
        not keyword.iskeyword(token.string)
        or not _is_classname(i)
        and not _is_varname(i)
    ):
      continue
    start_line, start_col = token.start
    end_line, end_col = token.end
    if start_line != end_line:
      continue
    line = lines[start_line - 1]
    new_line = (
        line[:start_col]
        + _keyword_to_parseable_name(token.string)
        + line[end_col:]
    )
    lines[start_line - 1] = new_line
  return "\n".join(lines)


def _parse(src: str, feature_version: int, filename: str = ""):
  """Call the ast parser with the appropriate feature version."""
  kwargs = {"feature_version": feature_version, "type_comments": True}
  try:
    ast_root_node = astlib.parse(src, filename, **kwargs)
  except SyntaxError as e:
    # We only attempt to fix the source code if a syntax error is encountered
    # because (1) this way, if the fixing fails, the error details will
    # correctly reflect the original source, and (2) fixing is unnecessary most
    # of the time, so always running it would be inefficient.
    fixed_src = _fix_src(src)
    try:
      ast_root_node = astlib.parse(fixed_src, filename, **kwargs)
    except SyntaxError:
      raise ParseError(
          e.msg, line=e.lineno, filename=filename, column=e.offset, text=e.text
      ) from e
  return ast_root_node


def _feature_version(python_version: tuple[int, ...]) -> int:
  """Get the python feature version for the parser."""
  if len(python_version) == 1:
    return sys.version_info.minor
  else:
    return python_version[1]


# Options that will be copied from pytype.config.Options.
_TOPLEVEL_PYI_OPTIONS = (
    "platform",
    "python_version",
    "strict_primitive_comparisons",
)


@dataclasses.dataclass
class PyiOptions:
  """Pyi parsing options."""

  python_version: tuple[int, int] = sys.version_info[:2]
  platform: str = sys.platform
  strict_primitive_comparisons: bool = True

  @classmethod
  def from_toplevel_options(cls, toplevel_options):
    kwargs = {}
    for k in _TOPLEVEL_PYI_OPTIONS:
      kwargs[k] = getattr(toplevel_options, k)
    return cls(**kwargs)


def parse_string(
    src: str,
    name: str | None = None,
    filename: str | None = None,
    options: PyiOptions | None = None,
):
  return parse_pyi(src, filename=filename, module_name=name, options=options)


def parse_pyi(
    src: str,
    filename: str | None,
    module_name: str,
    options: PyiOptions | None = None,
    debug_mode: bool = False,
) -> pytd.TypeDeclUnit:
  """Parse a pyi string."""
  filename = filename or ""
  options = options or PyiOptions()
  feature_version = _feature_version(options.python_version)
  root = _parse(src, feature_version, filename)
  if debug_mode:
    print(debug.dump(root, astlib, include_attributes=False))
  root = _ConvertConstantsVisitor(filename).visit(root)
  gen_pytd = _GeneratePytdVisitor(src, filename, module_name, options)
  root = gen_pytd.visit(root)
  if debug_mode:
    print("---transformed parse tree--------------------")
    print(root)
  root = post_process_ast(root, src, module_name)
  if debug_mode:
    print("---post-processed---------------------")
    print(root)
    print("------------------------")
    print(gen_pytd.defs.type_map)
    print(gen_pytd.defs.module_path_map)
  return root


def canonical_pyi(pyi, multiline_args=False, options=None):
  """Rewrite a pyi in canonical form."""
  ast = parse_string(pyi, options=options)
  ast = ast.Visit(visitors.ClassTypeToNamedType())
  ast = ast.Visit(visitors.CanonicalOrderingVisitor())
  ast.Visit(visitors.VerifyVisitor())
  return pytd_utils.Print(ast, multiline_args)
