"""Utilities for inline type annotations."""

import collections
from collections.abc import Sequence
import dataclasses
import itertools
from typing import Any

from pytype import state
from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.overlays import typing_overlay
from pytype.pytd import pytd_utils
from pytype.typegraph import cfg


@dataclasses.dataclass
class AnnotatedValue:
  typ: Any
  value: Any
  final: bool = False


class AnnotationUtils(utils.ContextWeakrefMixin):
  """Utility class for inline type annotations."""

  def sub_annotations(self, node, annotations, substs, instantiate_unbound):
    """Apply type parameter substitutions to a dictionary of annotations."""
    if substs and all(substs):
      return {
          name: self.sub_one_annotation(
              node, annot, substs, instantiate_unbound
          )
          for name, annot in annotations.items()
      }
    return annotations

  def _get_type_parameter_subst(
      self,
      node: cfg.CFGNode,
      annot: abstract.TypeParameter,
      substs: Sequence[dict[str, cfg.Variable]],
      instantiate_unbound: bool,
  ) -> abstract.BaseValue:
    """Helper for sub_one_annotation."""
    # We use the given substitutions to bind the annotation if
    # (1) every subst provides at least one binding, and
    # (2) none of the bindings are ambiguous, and
    # (3) at least one binding is non-empty.
    if all(
        annot.full_name in subst and subst[annot.full_name].bindings
        for subst in substs
    ):
      vals = sum((subst[annot.full_name].data for subst in substs), [])
    else:
      vals = None
    if (
        vals is None
        or any(isinstance(v, abstract.AMBIGUOUS) for v in vals)
        or all(isinstance(v, abstract.Empty) for v in vals)
    ):
      if instantiate_unbound:
        vals = annot.instantiate(node).data
      else:
        vals = [annot]
    return self.ctx.convert.merge_classes(vals)

  def sub_one_annotation(self, node, annot, substs, instantiate_unbound=True):

    def get_type_parameter_subst(annotation):
      return self._get_type_parameter_subst(
          node, annotation, substs, instantiate_unbound
      )

    return self._do_sub_one_annotation(node, annot, get_type_parameter_subst)

  def _do_sub_one_annotation(self, node, annot, get_type_parameter_subst_fn):
    """Apply type parameter substitutions to an annotation."""
    # We push annotations onto 'stack' and move them to the 'done' stack as they
    # are processed. For each annotation, we also track an 'inner_type_keys'
    # value, which is meaningful only for a NestedAnnotation. For a
    # NestedAnnotation, inner_type_keys=None indicates the annotation has not
    # yet been seen, so we push its inner types onto the stack, followed by the
    # annotation itself with its real 'inner_type_keys' value. When we see the
    # annotation again, we pull the processed inner types off the 'done' stack
    # and construct the final annotation.
    stack = [(annot, None)]
    late_annotations = {}
    done = []
    while stack:
      cur, inner_type_keys = stack.pop()
      if not cur.formal:
        done.append(cur)
      elif isinstance(cur, mixin.NestedAnnotation):
        if cur.is_late_annotation() and any(t[0] == cur for t in stack):
          # We've found a recursive type. We generate a LateAnnotation as a
          # placeholder for the substituted type.
          if cur not in late_annotations:
            param_strings = []
            for t in utils.unique_list(self.get_type_parameters(cur)):
              s = pytd_utils.Print(
                  get_type_parameter_subst_fn(t).to_pytd_type_of_instance(node)
              )
              param_strings.append(s)
            expr = f"{cur.expr}[{', '.join(param_strings)}]"
            late_annot = abstract.LateAnnotation(expr, cur.stack, cur.ctx)
            late_annotations[cur] = late_annot
          done.append(late_annotations[cur])
        elif inner_type_keys is None:
          keys, vals = zip(*cur.get_inner_types())
          stack.append((cur, keys))
          stack.extend((val, None) for val in vals)
        else:
          inner_types = []
          for k in inner_type_keys:
            inner_types.append((k, done.pop()))
          done_annot = cur.replace(inner_types)
          if cur in late_annotations:
            # If we've generated a LateAnnotation placeholder for cur's
            # substituted type, replace it now with the real type.
            late_annot = late_annotations.pop(cur)
            late_annot.set_type(done_annot)
            if "[" in late_annot.expr:
              if self.ctx.vm.late_annotations is None:
                self.ctx.vm.flatten_late_annotation(
                    node, late_annot, self.ctx.vm.frame.f_globals
                )
              else:
                self.ctx.vm.late_annotations[
                    late_annot.expr.split("[", 1)[0]
                ].append(late_annot)
          done.append(done_annot)
      else:
        done.append(get_type_parameter_subst_fn(cur))
    assert len(done) == 1
    return done[0]

  def sub_annotations_for_parameterized_class(
      self,
      cls: abstract.ParameterizedClass,
      annotations: dict[str, abstract.BaseValue],
  ) -> dict[str, abstract.BaseValue]:
    """Apply type parameter substitutions to a dictionary of annotations.

    Args:
      cls: ParameterizedClass that defines type parameter substitutions.
      annotations: A dictionary of annotations to which type parameter
        substition should be applied.

    Returns:
      Annotations with type parameters substituted.
    """
    formal_type_parameters = cls.get_formal_type_parameters()

    def get_type_parameter_subst(
        annotation: abstract.BaseValue,
    ) -> abstract.BaseValue | None:
      # Normally the type parameter module is set correctly at this point.
      # Except for the case when a method that references this type parameter
      # is inherited in a subclass that does not specialize this parameter:
      #   class A(Generic[T]):
      #     def f(self, t: T): ...
      #
      #   class B(Generic[T], A[T]):
      #     pass
      #
      #   class C(B[int]): ...
      # In this case t in A[T].f will be annotated with T with no module set,
      # since we don't know the correct module until T is specialized in
      # B[int].
      for name in (
          f"{cls.full_name}.{annotation.name}",
          f"{cls.name}.{annotation.name}",
      ):
        if name in formal_type_parameters:
          return formal_type_parameters[name]
      # Method parameter can be annotated with a typevar that doesn't
      # belong to the class template:
      #   class A(Generic[T]):
      #     def f(self, t: U): ...
      # In this case we return it as is.
      return annotation

    return {
        name: self._do_sub_one_annotation(
            self.ctx.root_node, annot, get_type_parameter_subst
        )
        for name, annot in annotations.items()
    }

  def get_late_annotations(self, annot):
    if annot.is_late_annotation() and not annot.resolved:
      yield annot
    elif isinstance(annot, mixin.NestedAnnotation):
      for _, typ in annot.get_inner_types():
        yield from self.get_late_annotations(typ)

  def add_scope(self, annot, types, cls, seen=None):
    """Add scope for type parameters.

    In original type class, all type parameters that should be added a scope
    will be replaced with a new copy.

    Args:
      annot: The type class.
      types: A type name list that should be added a scope.
      cls: The class that type parameters should be scoped to.
      seen: Already seen types.

    Returns:
      The type with fresh type parameters that have been added the scope.
    """
    if seen is None:
      seen = {annot}
    elif annot in seen or not annot.formal:
      return annot
    else:
      seen.add(annot)
    if isinstance(annot, abstract.TypeParameter):
      if annot.name in types:
        return annot.with_scope(cls.full_name)
      elif annot.full_name == "typing.Self":
        bound_annot = annot.copy()
        bound_annot.bound = cls
        return bound_annot
      else:
        return annot
    elif isinstance(annot, mixin.NestedAnnotation):
      inner_types = [
          (key, self.add_scope(typ, types, cls, seen))
          for key, typ in annot.get_inner_types()
      ]
      return annot.replace(inner_types)
    return annot

  def get_type_parameters(self, annot, seen=None):
    """Returns all the TypeParameter instances that appear in the annotation.

    Note that if you just need to know whether or not the annotation contains
    type parameters, you can check its `.formal` attribute.

    Args:
      annot: An annotation.
      seen: A seen set.
    """
    seen = seen or set()
    if annot in seen or not annot.formal:
      return []
    if isinstance(annot, mixin.NestedAnnotation):
      # We track parameterized classes to avoid recursion errors when a class
      # contains itself.
      seen = seen | {annot}
    if isinstance(annot, abstract.TypeParameter):
      return [annot]
    elif isinstance(annot, abstract.TupleClass):
      annots = []
      for idx in range(annot.tuple_length):
        annots.extend(
            self.get_type_parameters(annot.formal_type_parameters[idx], seen)
        )
      return annots
    elif isinstance(annot, mixin.NestedAnnotation):
      return sum(
          (
              self.get_type_parameters(t, seen)
              for _, t in annot.get_inner_types()
          ),
          [],
      )
    return []

  def get_callable_type_parameter_names(self, val: abstract.BaseValue):
    """Gets all TypeParameter names that appear in a Callable in 'val'."""
    type_params = set()
    seen = set()
    stack = [val]
    while stack:
      annot = stack.pop()
      if annot in seen or not annot.formal:
        continue
      seen.add(annot)
      if annot.full_name == "typing.Callable":
        params = collections.Counter(self.get_type_parameters(annot))
        if isinstance(annot, abstract.CallableClass):
          # pytype represents Callable[[T1, T2], None] as
          # CallableClass({0: T1, 1: T2, ARGS: Union[T1, T2], RET: None}),
          # so we have to fix double-counting of argument type parameters.
          params -= collections.Counter(
              self.get_type_parameters(
                  annot.formal_type_parameters[abstract_utils.ARGS]
              )
          )
        # Type parameters that appear only once in a function signature are
        # invalid, so ignore them.
        type_params.update(p.name for p, n in params.items() if n > 1)
      elif isinstance(annot, mixin.NestedAnnotation):
        stack.extend(v for _, v in annot.get_inner_types())
    return type_params

  def convert_function_type_annotation(self, name, typ):
    visible = typ.data
    if len(visible) > 1:
      self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, visible, name)
      return None
    else:
      return visible[0]

  def convert_function_annotations(self, node, raw_annotations):
    """Convert raw annotations to a {name: annotation} dict."""
    if raw_annotations:
      # {"i": int, "return": str} is stored as (int, str, ("i", "return"))
      names = abstract_utils.get_atomic_python_constant(raw_annotations[-1])
      type_list = raw_annotations[:-1]
      annotations_list = []
      for name, t in zip(names, type_list):
        name = abstract_utils.get_atomic_python_constant(name)
        t = self.convert_function_type_annotation(name, t)
        annotations_list.append((name, t))
      return self.convert_annotations_list(node, annotations_list)
    else:
      return {}

  def convert_annotations_list(self, node, annotations_list):
    """Convert a (name, raw_annot) list to a {name: annotation} dict."""
    annotations = {}
    for name, t in annotations_list:
      if t is None or abstract_utils.is_ellipsis(t):
        # '...' is an experimental "inferred type"; see b/213607272.
        continue
      annot = self._process_one_annotation(
          node, t, name, self.ctx.vm.simple_stack()
      )
      if annot is not None:
        annotations[name] = annot
    return annotations

  def convert_class_annotations(self, node, raw_annotations):
    """Convert a name -> raw_annot dict to annotations."""
    annotations = {}
    raw_items = raw_annotations.items()
    for name, t in raw_items:
      # Don't use the parameter name, since it's often something unhelpful
      # like `0`.
      annot = self._process_one_annotation(
          node, t, None, self.ctx.vm.simple_stack()
      )
      annotations[name] = annot or self.ctx.convert.unsolvable
    return annotations

  def init_annotation(self, node, name, annot, container=None, extra_key=None):
    value = self.ctx.vm.init_class(
        node, annot, container=container, extra_key=extra_key
    )
    for d in value.data:
      d.from_annotation = name
    return node, value

  def _in_class_frame(self):
    frame = self.ctx.vm.frame
    if not frame.func:
      return False
    return (
        isinstance(frame.func.data, abstract.BoundFunction)
        or frame.func.data.is_attribute_of_class
    )

  def extract_and_init_annotation(self, node, name, var):
    """Extracts an annotation from var and instantiates it."""
    frame = self.ctx.vm.frame
    substs = frame.substs
    if self._in_class_frame():
      self_var = frame.first_arg
      if self_var:
        # self_var is an instance of (a subclass of) the class on which
        # frame.func is defined. We walk self_var's class's MRO to find the
        # defining class and grab its type parameter substitutions.
        defining_cls_name, _, _ = frame.func.data.name.rpartition(".")
        type_params = []
        defining_classes = []
        for v in self_var.data:
          v_cls = v if isinstance(v, abstract.Class) else v.cls
          for cls in v_cls.mro:
            if cls.name == defining_cls_name:
              # Normalize type parameter names by dropping the scope.
              type_params.extend(p.with_scope(None) for p in cls.template)
              defining_classes.append(cls)
              break
        self_substs = tuple(
            abstract_utils.get_type_parameter_substitutions(cls, type_params)
            for cls in defining_classes
        )
        substs = abstract_utils.combine_substs(substs, self_substs)
    typ = self.extract_annotation(
        node,
        var,
        name,
        self.ctx.vm.simple_stack(),
        allowed_type_params=set(itertools.chain(*substs)),
    )
    if isinstance(typ, typing_overlay.Final):
      return typ, self.ctx.new_unsolvable(node)
    return self._sub_and_instantiate(node, name, typ, substs)

  def _sub_and_instantiate(self, node, name, typ, substs):
    if isinstance(typ, abstract.FinalAnnotation):
      t, value = self._sub_and_instantiate(node, name, typ.annotation, substs)
      return abstract.FinalAnnotation(t, self.ctx), value
    if typ.formal:
      substituted_type = self.sub_one_annotation(
          node, typ, substs, instantiate_unbound=False
      )
    else:
      substituted_type = typ
    if typ.formal and self._in_class_frame():
      class_substs = abstract_utils.combine_substs(
          substs, [{"typing.Self": self.ctx.vm.frame.first_arg}]
      )
      type_for_value = self.sub_one_annotation(
          node, typ, class_substs, instantiate_unbound=False
      )
    else:
      type_for_value = substituted_type
    _, value = self.init_annotation(node, name, type_for_value)
    return substituted_type, value

  def apply_annotation(self, node, op, name, value):
    """If there is an annotation for the op, return its value."""
    assert op is self.ctx.vm.frame.current_opcode
    if op.code.filename != self.ctx.vm.filename:
      return AnnotatedValue(None, value)
    if not op.annotation:
      return AnnotatedValue(None, value)
    annot = op.annotation
    if annot == "...":
      # Experimental "inferred type": see b/213607272.
      return AnnotatedValue(None, value)
    frame = self.ctx.vm.frame
    stack = self.ctx.vm.simple_stack()
    with self.ctx.vm.generate_late_annotations(stack):
      var, errorlog = abstract_utils.eval_expr(
          self.ctx, node, frame.f_globals, frame.f_locals, annot
      )
    if errorlog:
      self.ctx.errorlog.invalid_annotation(
          self.ctx.vm.frames, annot, details=errorlog.details
      )
    typ, annot_val = self.extract_and_init_annotation(node, name, var)
    if isinstance(typ, typing_overlay.Final):
      # return the original value, we want type inference here.
      return AnnotatedValue(None, value, final=True)
    elif isinstance(typ, abstract.FinalAnnotation):
      return AnnotatedValue(typ.annotation, annot_val, final=True)
    elif typ.full_name == "typing.TypeAlias":
      # Validate that 'value' is a legal type alias and use it.
      annot = self.extract_annotation(node, value, name, stack)
      return AnnotatedValue(None, annot.to_variable(node))
    else:
      return AnnotatedValue(typ, annot_val)

  def extract_annotation(
      self,
      node,
      var,
      name,
      stack,
      allowed_type_params: set[str] | None = None,
  ):
    """Returns an annotation extracted from 'var'.

    Args:
      node: The current node.
      var: The variable to extract from.
      name: The annotated name.
      stack: The frame stack.
      allowed_type_params: Type parameters that are allowed to appear in the
        annotation. 'None' means all are allowed. If non-None, the result of
        calling get_callable_type_parameter_names on the extracted annotation is
        also added to the allowed set.
    """
    try:
      typ = abstract_utils.get_atomic_value(var)
    except abstract_utils.ConversionError:
      self.ctx.errorlog.ambiguous_annotation(self.ctx.vm.frames, None, name)
      return self.ctx.convert.unsolvable
    typ = self._process_one_annotation(node, typ, name, stack)
    if not typ:
      return self.ctx.convert.unsolvable
    if typ.formal and allowed_type_params is not None:
      allowed_type_params = (
          allowed_type_params | self.get_callable_type_parameter_names(typ)
      )
      if self.ctx.vm.frame.func and (
          isinstance(self.ctx.vm.frame.func.data, abstract.BoundFunction)
          or self.ctx.vm.frame.func.data.is_class_builder
      ):
        allowed_type_params.add("typing.Self")
      illegal_params = []
      for x in self.get_type_parameters(typ):
        if not allowed_type_params.intersection([x.name, x.full_name]):
          illegal_params.append(x.name)
      if illegal_params:
        self._log_illegal_params(illegal_params, stack, typ, name)
        return self.ctx.convert.unsolvable
    return typ

  def _log_illegal_params(self, illegal_params, stack, typ, name):
    out_of_scope_params = utils.unique_list(illegal_params)
    details = "TypeVar(s) %s not in scope" % ", ".join(
        repr(p) for p in out_of_scope_params
    )
    if self.ctx.vm.frame.func:
      method = self.ctx.vm.frame.func.data
      if isinstance(method, abstract.BoundFunction):
        desc = "class"
        frame_name = method.name.rsplit(".", 1)[0]
      else:
        desc = "class" if method.is_class_builder else "method"
        frame_name = method.name
      details += f" for {desc} {frame_name!r}"
    if "AnyStr" in out_of_scope_params:
      str_type = "Union[str, bytes]"
      details += f"\nNote: For all string types, use {str_type}."
    self.ctx.errorlog.invalid_annotation(stack, typ, details, name)

  def eval_multi_arg_annotation(self, node, func, annot, stack):
    """Evaluate annotation for multiple arguments (from a type comment)."""
    args, errorlog = self._eval_expr_as_tuple(node, annot, stack)
    if errorlog:
      self.ctx.errorlog.invalid_function_type_comment(
          stack, annot, details=errorlog.details
      )
    code = func.code
    expected = code.get_arg_count()
    names = code.varnames

    # This is a hack.  Specifying the type of the first arg is optional in
    # class and instance methods.  There is no way to tell at this time
    # how the function will be used, so if the first arg is self or cls we
    # make it optional.  The logic is somewhat convoluted because we don't
    # want to count the skipped argument in an error message.
    if len(args) != expected:
      if expected and names[0] in ["self", "cls"]:
        expected -= 1
        names = names[1:]

    if len(args) != expected:
      self.ctx.errorlog.invalid_function_type_comment(
          stack,
          annot,
          details="Expected %d args, %d given" % (expected, len(args)),
      )
      return
    for name, arg in zip(names, args):
      resolved = self._process_one_annotation(node, arg, name, stack)
      if resolved is not None:
        func.signature.set_annotation(name, resolved)

  def _process_one_annotation(
      self,
      node: cfg.CFGNode,
      annotation: abstract.BaseValue,
      # We require `stack` to be a tuple to make sure we pass in a frozen
      # snapshot of the frame stack, rather than the actual stack, since late
      # annotations need to snapshot the stack at time of creation in order to
      # get the right line information for error messages.
      name: str | None,
      stack: tuple[state.FrameType, ...],
  ) -> abstract.BaseValue | None:
    """Change annotation / record errors where required."""
    if isinstance(annotation, abstract.AnnotationContainer):
      annotation = annotation.base_cls

    if isinstance(annotation, typing_overlay.Union):
      self.ctx.errorlog.invalid_annotation(
          stack, annotation, "Needs options", name
      )
      return None
    elif (
        name is not None
        and name != "return"
        and annotation.full_name in abstract_utils.TYPE_GUARDS
    ):
      self.ctx.errorlog.invalid_annotation(
          stack,
          annotation,
          f"{annotation.name} is only allowed as a return annotation",
          name,
      )
      return None
    elif (
        isinstance(annotation, abstract.Instance)
        and annotation.cls == self.ctx.convert.str_type
    ):
      # String annotations : Late evaluation
      if isinstance(annotation, abstract.PythonConstant):
        expr = annotation.pyval
        if not expr:
          self.ctx.errorlog.invalid_annotation(
              stack, annotation, "Cannot be an empty string", name
          )
          return None
        frame = self.ctx.vm.frame
        # Immediately try to evaluate the reference, generating LateAnnotation
        # objects as needed. We don't store the entire string as a
        # LateAnnotation because:
        # - With __future__.annotations, all annotations look like forward
        #   references - most of them don't need to be late evaluated.
        # - Given an expression like "Union[str, NotYetDefined]", we want to
        #   evaluate the union immediately so we don't end up with a complex
        #   LateAnnotation, which can lead to bugs when instantiated.
        with self.ctx.vm.generate_late_annotations(stack):
          v, errorlog = abstract_utils.eval_expr(
              self.ctx, node, frame.f_globals, frame.f_locals, expr
          )
        if errorlog:
          self.ctx.errorlog.copy_from(errorlog.errors, stack)
        if len(v.data) == 1:
          return self._process_one_annotation(node, v.data[0], name, stack)
      self.ctx.errorlog.ambiguous_annotation(stack, [annotation], name)
      return None
    elif annotation.cls == self.ctx.convert.none_type:
      # PEP 484 allows to write "NoneType" as "None"
      return self.ctx.convert.none_type
    elif isinstance(annotation, mixin.NestedAnnotation):
      if annotation.processed:
        return annotation
      annotation.processed = True
      for key, typ in annotation.get_inner_types():
        if (
            annotation.full_name == "typing.Callable"
            and key == abstract_utils.RET
        ):
          inner_name = "return"
        else:
          inner_name = name
        processed = self._process_one_annotation(node, typ, inner_name, stack)
        if processed is None:
          return None
        elif (
            name == inner_name
            and processed.full_name in abstract_utils.TYPE_GUARDS
        ):
          self.ctx.errorlog.invalid_annotation(
              stack, typ, f"{processed.name} is not allowed as inner type", name
          )
          return None
        annotation.update_inner_type(key, processed)
      return annotation
    elif isinstance(
        annotation,
        (
            abstract.Class,
            abstract.AMBIGUOUS_OR_EMPTY,
            abstract.TypeParameter,
            abstract.ParamSpec,
            abstract.ParamSpecArgs,
            abstract.ParamSpecKwargs,
            abstract.Concatenate,
            abstract.FinalAnnotation,
            function.ParamSpecMatch,
            typing_overlay.Final,
            typing_overlay.Never,
        ),
    ):
      return annotation
    else:
      self.ctx.errorlog.invalid_annotation(
          stack, annotation, "Not a type", name
      )
      return None

  def _eval_expr_as_tuple(self, node, expr, stack):
    """Evaluate an expression as a tuple."""
    if not expr:
      return (), None

    f_globals = self.ctx.vm.frame.f_globals
    f_locals = self.ctx.vm.frame.f_locals
    with self.ctx.vm.generate_late_annotations(stack):
      result_var, errorlog = abstract_utils.eval_expr(
          self.ctx, node, f_globals, f_locals, expr
      )
    result = abstract_utils.get_atomic_value(result_var)
    # If the result is a tuple, expand it.
    if isinstance(result, abstract.PythonConstant) and isinstance(
        result.pyval, tuple
    ):
      return (
          tuple(abstract_utils.get_atomic_value(x) for x in result.pyval),
          errorlog,
      )
    else:
      return (result,), errorlog

  def deformalize(self, value):
    # TODO(rechen): Instead of doing this, call sub_one_annotation() to replace
    # type parameters with their bound/constraints.
    while value.formal:
      if isinstance(value, abstract.ParameterizedClass):
        value = value.base_cls
      else:
        value = self.ctx.convert.unsolvable
    return value
