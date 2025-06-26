"""Abstract attribute handling."""

import logging
from typing import Optional

from pytype import datatypes
from pytype import utils
from pytype.abstract import abstract
from pytype.abstract import abstract_utils
from pytype.abstract import function
from pytype.abstract import mixin
from pytype.errors import error_types
from pytype.overlays import overlay
from pytype.overlays import special_builtins
from pytype.typegraph import cfg

log = logging.getLogger(__name__)

_NodeAndMaybeVarType = tuple[cfg.CFGNode, Optional[cfg.Variable]]


class AbstractAttributeHandler(utils.ContextWeakrefMixin):
  """Handler for abstract attributes."""

  def get_attribute(self, node, obj, name, valself=None):
    """Get the named attribute from the given object.

    Args:
      node: The current CFG node.
      obj: The object.
      name: The name of the attribute to retrieve.
      valself: A cfg.Binding to a self reference to include in the attribute's
        origins. If obj is an abstract.Class, valself can be a binding to:
        * an instance of obj - obj will be treated strictly as a class.
        * obj itself - obj will be treated as an instance of its metaclass.
        * None - if name == "__getitem__", obj is a type annotation; else, obj
            is strictly a class, but the attribute is left unbound.
        Else, valself is optional and should be a binding to obj when given.

    Returns:
      A tuple (CFGNode, cfg.Variable). If this attribute doesn't exist,
      the Variable will be None.
    """
    obj = abstract_utils.unwrap_final(obj)
    # Some objects have special attributes, like "__get__" or "__iter__"
    special_attribute = obj.get_special_attribute(node, name, valself)
    if special_attribute is not None:
      return node, special_attribute
    if isinstance(obj, abstract.Function):
      if name == "__get__":
        # The pytd for "function" has a __get__ attribute, but if we already
        # have a function we don't want to be treated as a descriptor.
        return node, None
      else:
        return self._get_instance_attribute(node, obj, name, valself)
    elif isinstance(obj, abstract.ParameterizedClass):
      return self.get_attribute(node, obj.base_cls, name, valself)
    elif isinstance(obj, abstract.Class):
      return self._get_class_attribute(node, obj, name, valself)
    elif isinstance(obj, overlay.Overlay):
      return self._get_module_attribute(
          node, obj.get_module(name), name, valself
      )
    elif isinstance(obj, abstract.Module):
      return self._get_module_attribute(node, obj, name, valself)
    elif isinstance(obj, abstract.SimpleValue):
      return self._get_instance_attribute(node, obj, name, valself)
    elif isinstance(obj, abstract.Union):
      if name == "__getitem__":
        # __getitem__ is implemented in abstract.Union.getitem_slot.
        return node, self.ctx.new_unsolvable(node)
      nodes = []
      ret = self.ctx.program.NewVariable()
      for o in obj.options:
        node2, attr = self.get_attribute(node, o, name, valself)
        if attr is not None:
          ret.PasteVariable(attr, node2)
          nodes.append(node2)
      if ret.bindings:
        return self.ctx.join_cfg_nodes(nodes), ret
      else:
        return node, None
    elif isinstance(obj, special_builtins.SuperInstance):
      return self._get_attribute_from_super_instance(node, obj, name, valself)
    elif isinstance(obj, special_builtins.Super):
      return self.get_attribute(
          node, self.ctx.convert.super_type, name, valself
      )
    elif isinstance(obj, (abstract.StaticMethod, abstract.ClassMethod)):
      return self.get_attribute(node, obj.method, name, valself)
    elif isinstance(obj, abstract.BoundFunction):
      return self.get_attribute(node, obj.underlying, name, valself)
    elif isinstance(obj, abstract.TypeParameterInstance):
      param_var = obj.instance.get_instance_type_parameter(obj.name)
      if not param_var.bindings:
        param_var = obj.param.instantiate(self.ctx.root_node)
      results = []
      nodes = []
      for b in param_var.bindings:
        if b.data == obj:
          continue
        node2, ret = self.get_attribute(node, b.data, name, valself)
        if ret is None:
          if b.IsVisible(node):
            return node, None
        else:
          results.append(ret)
          nodes.append(node2)
      if nodes:
        node = self.ctx.join_cfg_nodes(nodes)
        return node, self.ctx.join_variables(node, results)
      else:
        return node, self.ctx.new_unsolvable(node)
    elif isinstance(obj, abstract.Empty):
      return node, None
    elif isinstance(obj, abstract.ParamSpec):
      if name == "args":
        return node, abstract.ParamSpecArgs(obj, self.ctx).to_variable(node)
      elif name == "kwargs":
        return node, abstract.ParamSpecKwargs(obj, self.ctx).to_variable(node)
      else:
        return node, None
    else:
      return node, None

  def set_attribute(
      self,
      node: cfg.CFGNode,
      obj: abstract.BaseValue,
      name: str,
      value: cfg.Variable,
  ) -> cfg.CFGNode:
    """Set an attribute on an object.

    The attribute might already have a Variable in it and in that case we cannot
    overwrite it and instead need to add the elements of the new variable to the
    old variable.

    Args:
      node: The current CFG node.
      obj: The object.
      name: The name of the attribute to set.
      value: The Variable to store in it.

    Returns:
      A (possibly changed) CFG node.
    Raises:
      AttributeError: If the attribute cannot be set.
      NotImplementedError: If attribute setting is not implemented for obj.
    """
    if not self._check_writable(obj, name):
      # We ignore the write of an attribute that's not in __slots__, since it
      # wouldn't happen in the Python interpreter, either.
      return node
    if self.ctx.vm.frame is not None and obj is self.ctx.vm.frame.f_globals:
      for v in value.data:
        v.update_official_name(name)
    if isinstance(obj, abstract.Empty):
      return node
    elif isinstance(obj, abstract.Module):
      # Assigning attributes on modules is pretty common. E.g.
      # sys.path, sys.excepthook.
      log.warning("Ignoring overwrite of %s.%s", obj.name, name)
      return node
    elif isinstance(obj, (abstract.StaticMethod, abstract.ClassMethod)):
      return self.set_attribute(node, obj.method, name, value)
    elif isinstance(obj, abstract.SimpleValue):
      return self._set_member(node, obj, name, value)
    elif isinstance(obj, abstract.BoundFunction):
      return self.set_attribute(node, obj.underlying, name, value)
    elif isinstance(obj, abstract.Unsolvable):
      return node
    elif isinstance(obj, abstract.Unknown):
      if name in obj.members:
        obj.members[name].PasteVariable(value, node)
      else:
        obj.members[name] = value.AssignToNewVariable(node)
      return node
    elif isinstance(obj, abstract.TypeParameterInstance):
      nodes = []
      for v in obj.instance.get_instance_type_parameter(obj.name).data:
        nodes.append(self.set_attribute(node, v, name, value))
      return self.ctx.join_cfg_nodes(nodes) if nodes else node
    elif isinstance(obj, abstract.Union):
      for option in obj.options:
        node = self.set_attribute(node, option, name, value)
      return node
    else:
      raise NotImplementedError(obj.__class__.__name__)

  def _check_writable(self, obj, name):
    """Verify that a given attribute is writable. Log an error if not."""
    if not obj.cls.mro:
      # "Any" etc.
      return True
    for baseclass in obj.cls.mro:
      if baseclass.full_name == "builtins.object":
        # It's not possible to set an attribute on object itself.
        # (object has __setattr__, but that honors __slots__.)
        continue
      if isinstance(baseclass, abstract.SimpleValue) and (
          "__setattr__" in baseclass or name in baseclass
      ):
        return True  # This is a programmatic attribute.
      if baseclass.slots is None or name in baseclass.slots:
        return True  # Found a slot declaration; this is an instance attribute
    self.ctx.errorlog.not_writable(self.ctx.vm.frames, obj, name)
    return False

  def _should_look_for_submodule(
      self, module: abstract.Module, attr_var: cfg.Variable | None
  ):
    # Given a module and an attribute looked up from its contents, determine
    # whether a possible submodule with the same name as the attribute should
    # take precedence over the attribute.
    if attr_var is None:
      return True
    attr_cls = self.ctx.convert.merge_classes(attr_var.data)
    if attr_cls == self.ctx.convert.module_type and not any(
        isinstance(attr, abstract.Module) for attr in attr_var.data
    ):
      # The attribute is an abstract.Instance(module), which returns Any for all
      # attribute accesses, so we should try to find the actual submodule.
      return True
    if (
        f"{module.name}.__init__" == self.ctx.options.module_name
        and attr_var.data == [self.ctx.convert.unsolvable]
    ):
      # There's no reason for a module's __init__ file to look up attributes in
      # itself, so attr_var is a submodule whose type was inferred as Any during
      # a first-pass analysis with incomplete type information.
      return True
    # Otherwise, local variables in __init__.py take precedence over submodules.
    return False

  def _get_module_attribute(
      self,
      node: cfg.CFGNode,
      module: abstract.Module,
      name: str,
      valself: cfg.Binding | None = None,
  ) -> _NodeAndMaybeVarType:
    """Get an attribute from a module."""
    try:
      node, var = self._get_instance_attribute(node, module, name, valself)
    except abstract_utils.ModuleLoadError:
      var = None

    if not self._should_look_for_submodule(module, var):
      return node, var

    # Look for a submodule. If none is found, then return `var` instead, which
    # may be a submodule that appears only in __init__.
    return node, module.get_submodule(node, name) or var

  def _get_class_attribute(
      self,
      node: cfg.CFGNode,
      cls: abstract.Class,
      name: str,
      valself: cfg.Binding | None = None,
  ) -> _NodeAndMaybeVarType:
    """Get an attribute from a class."""
    if (
        not valself
        or not abstract_utils.equivalent_to(valself, cls)
        or cls == self.ctx.convert.type_type
    ):
      # Since type(type) == type, the type_type check prevents an infinite loop.
      meta = None
    else:
      # We treat a class as an instance of its metaclass, but only if we are
      # looking for a class rather than an instance attribute. (So, for
      # instance, if we're analyzing int.mro(), we want to retrieve the mro
      # method on the type class, but for (3).mro(), we want to report that the
      # method does not exist.)
      meta = cls.cls
    return self._get_attribute(node, cls, meta, name, valself)

  def _get_instance_attribute(
      self,
      node: cfg.CFGNode,
      obj: abstract.SimpleValue,
      name: str,
      valself: cfg.Binding | None = None,
  ) -> _NodeAndMaybeVarType:
    """Get an attribute from an instance."""
    cls = None if obj.cls.full_name == "builtins.type" else obj.cls
    try:
      return self._get_attribute(node, obj, cls, name, valself)
    except abstract.SignatureMutationError as e:
      # This prevents a crash where we are trying to create a PyTDClass, and
      # one of its methods has an invalid `self` annotation.  This will
      # typically be caught via the wrong-arg-types check, but there seems to
      # be a corner case where we hit this code path and pytype raises an
      # exception.
      # See tests/test_generic2: test_invalid_mutation
      self.ctx.errorlog.invalid_signature_mutation(
          self.ctx.vm.frames, f"{obj.name}.{name}", e.pytd_sig
      )
      return node, self.ctx.new_unsolvable(node)

  def _get_attribute(self, node, obj, cls, name, valself):
    """Get an attribute from an object or its class.

    The underlying method called by all of the (_)get_(x_)attribute methods.
    Attempts to resolve an attribute first with __getattribute__, then by
    fetching it from the object, then by fetching it from the class, and
    finally with __getattr__.

    Arguments:
      node: The current node.
      obj: The object.
      cls: The object's class, may be None.
      name: The attribute name.
      valself: The binding to the self reference.

    Returns:
      A tuple of the node and the attribute, or None if it was not found.
    """
    if cls:
      # A __getattribute__ on the class controls all attribute access.
      node, attr = self._get_attribute_computed(
          node, cls, name, valself, compute_function="__getattribute__"
      )
    else:
      attr = None
    if attr is None:
      # Check for the attribute on the instance.
      if isinstance(obj, abstract.Class):
        # A class is an instance of its metaclass.
        node, attr = self._lookup_from_mro_and_handle_descriptors(
            node, obj, name, valself, skip=()
        )
      else:
        node, attr = self._get_member(node, obj, name, valself)
    # If the VM hit maximum depth while initializing this instance, it may have
    # attributes that we don't know about.
    is_unknown_instance_attribute = attr is None and obj.maybe_missing_members
    if attr is None and cls:
      # Check for the attribute on the class.
      node, attr = self.get_attribute(node, cls, name, valself)
      if attr:
        # If the attribute is a method, then we allow it to take precedence over
        # the possible unknown instance attribute, since otherwise method lookup
        # on classes with _HAS_DYNAMIC_ATTRIBUTES would always return Any. We
        # look up the attribute a second time, using a lookup method that leaves
        # properties as methods, so that properties are not replaced with Any.
        if is_unknown_instance_attribute:
          attr2 = self._lookup_from_mro(node, cls, name, valself, ())
          if any(isinstance(v, abstract.FUNCTION_TYPES) for v in attr2.data):
            is_unknown_instance_attribute = False
      elif not is_unknown_instance_attribute:
        # Fall back to __getattr__ if the attribute doesn't otherwise exist.
        node, attr = self._get_attribute_computed(
            node, cls, name, valself, compute_function="__getattr__"
        )
    if is_unknown_instance_attribute:
      attr = self.ctx.new_unsolvable(node)
    if attr is not None:
      attr = self._filter_var(node, attr)
    return node, attr

  def _get_attribute_from_super_instance(
      self, node, obj: special_builtins.SuperInstance, name, valself
  ):
    """Get an attribute from a super instance."""
    # A SuperInstance has `super_cls` and `super_obj` attributes recording the
    # arguments that super was (explicitly or implicitly) called with. For
    # example, if the call is `super(Foo, self)`, then super_cls=Foo,
    # super_obj=self. When the arguments  are omitted, super_cls and super_obj
    # are inferred from the surrounding context.
    if obj.super_obj:
      # When we have a chain of super calls, `starting_cls` is the cls in which
      # the first call was made, and `current_cls` is the one currently being
      # processed. E.g., for:
      #  class Foo(Bar):
      #    def __init__(self):
      #      super().__init__()  # line 3
      #  class Bar(Baz):
      #    def __init__(self):
      #      super().__init__()  # line 6
      # if we're looking up super.__init__ in line 6 as part of analyzing the
      # super call in line 3, then starting_cls=Foo, current_cls=Bar.
      if (
          obj.super_obj.cls.full_name == "builtins.type"
          or isinstance(obj.super_obj.cls, abstract.AMBIGUOUS_OR_EMPTY)
          or isinstance(obj.super_cls, abstract.AMBIGUOUS_OR_EMPTY)
      ):
        # Setting starting_cls to the current class when either of them is
        # ambiguous is technically incorrect but behaves correctly in the common
        # case of there being only a single super call.
        starting_cls = obj.super_cls
      elif obj.super_cls in obj.super_obj.mro:  # super() in a classmethod
        starting_cls = obj.super_obj
      else:
        starting_cls = obj.super_obj.cls
      current_cls = obj.super_cls
      valself = obj.super_obj.to_binding(node)
      # When multiple inheritance is present, the two classes' MROs may differ.
      # In this case, we want to use the MRO of starting_cls but skip all the
      # classes up to and including current_cls.
      skip = set()
      for base in starting_cls.mro:
        skip.add(base)
        if base.full_name == current_cls.full_name:
          break
    else:
      starting_cls = self.ctx.convert.super_type
      skip = ()
    return self._lookup_from_mro_and_handle_descriptors(
        node, starting_cls, name, valself, skip
    )

  def _lookup_from_mro_and_handle_descriptors(
      self, node, cls, name, valself, skip
  ):
    attr = self._lookup_from_mro(node, cls, name, valself, skip)
    if not attr.bindings:
      return node, None
    if isinstance(cls, abstract.InterpreterClass):
      result = self.ctx.program.NewVariable()
      nodes = []
      # Deal with descriptors as a potential additional level of indirection.
      for v in attr.bindings:
        value = v.data
        if (
            isinstance(value, special_builtins.PropertyInstance)
            and valself
            and valself.data == cls
        ):
          node2, getter = node, None
        else:
          node2, getter = self.get_attribute(node, value, "__get__", v)
        if getter is not None:
          posargs = []
          if valself and valself.data != cls:
            posargs.append(valself.AssignToNewVariable())
          else:
            posargs.append(self.ctx.convert.none.to_variable(node))
          posargs.append(cls.to_variable(node))
          node2, get_result = function.call_function(
              self.ctx, node2, getter, function.Args(tuple(posargs))
          )
          result.PasteVariable(get_result)
        else:
          result.PasteBinding(v, node2)
        nodes.append(node2)
      if nodes:
        return self.ctx.join_cfg_nodes(nodes), result
    return node, attr

  def _computable(self, name):
    return not (name.startswith("__") and name.endswith("__"))

  def _get_attribute_computed(
      self,
      node: cfg.CFGNode,
      cls: abstract.Class | abstract.AmbiguousOrEmptyType,
      name: str,
      valself: cfg.Binding,
      compute_function: str,
  ) -> _NodeAndMaybeVarType:
    """Call compute_function (if defined) to compute an attribute."""
    if (
        valself
        and not isinstance(valself.data, abstract.Module)
        and self._computable(name)
    ):
      attr_var = self._lookup_from_mro(
          node,
          cls,
          compute_function,
          valself,
          skip={self.ctx.convert.object_type},
      )
      if attr_var and attr_var.bindings:
        name_var = self.ctx.convert.constant_to_var(name, node=node)
        return function.call_function(
            self.ctx, node, attr_var, function.Args((name_var,))
        )
    return node, None

  def _lookup_variable_annotation(self, node, base, name, valself):
    if not isinstance(base, abstract.Class):
      return None, None
    annots = abstract_utils.get_annotations_dict(base.members)
    if not annots:
      return None, None
    typ = annots.get_type(node, name)
    if not typ or not typ.formal:
      return typ, None
    # The attribute contains a class-scoped type parameter, so we need to
    # reinitialize it with the current instance's parameter values. In this
    # case, we use the type from the annotation regardless of whether the
    # attribute is otherwise defined.
    if isinstance(base, abstract.ParameterizedClass):
      typ = self.ctx.annotation_utils.sub_annotations_for_parameterized_class(
          base, {name: typ}
      )[name]
    elif valself:
      subst = abstract_utils.get_type_parameter_substitutions(
          valself.data, self.ctx.annotation_utils.get_type_parameters(typ)
      )
      typ = self.ctx.annotation_utils.sub_one_annotation(
          node, typ, [subst], instantiate_unbound=False
      )
    else:
      return typ, None
    if typ.formal and valself:
      if isinstance(valself.data, abstract.Class):
        self_var = valself.data.instantiate(node)
      else:
        self_var = valself.AssignToNewVariable(node)
      typ = self.ctx.annotation_utils.sub_one_annotation(
          node, typ, [{"typing.Self": self_var}], instantiate_unbound=False
      )
    _, attr = self.ctx.annotation_utils.init_annotation(node, name, typ)
    return typ, attr

  def _lookup_from_mro_flat(self, node, base, name, valself, skip):
    """Look for an identifier in a single base class from an MRO."""
    # Potentially skip part of MRO, for super()
    if base in skip:
      return None
    # Check if the attribute is declared via a variable annotation.
    typ, attr = self._lookup_variable_annotation(node, base, name, valself)
    if attr is not None:
      return attr
    # When a special attribute is defined on a class buried in the MRO,
    # get_attribute (which calls get_special_attribute) is never called on
    # that class, so we have to call get_special_attribute here as well.
    var = base.get_special_attribute(node, name, valself)
    if var is None:
      node, var = self._get_attribute_flat(node, base, name, valself)
    if var is None or not var.bindings:
      # If the attribute is undefined, we use the annotated type, if any.
      if typ:
        _, attr = self.ctx.annotation_utils.init_annotation(node, name, typ)
        return attr
      return None
    return var

  def _lookup_from_mro(self, node, cls, name, valself, skip):
    """Find an identifier in the MRO of the class."""
    if isinstance(cls, (abstract.Unknown, abstract.Unsolvable)):
      # We don't know the object's MRO, so it's possible that one of its
      # bases has the attribute.
      return self.ctx.new_unsolvable(node)
    ret = self.ctx.program.NewVariable()
    add_origins = [valself] if valself else []
    for base in cls.mro:
      var = self._lookup_from_mro_flat(node, base, name, valself, skip)
      if var is None:
        continue
      for varval in var.bindings:
        value = varval.data
        if valself:
          # Check if we got a PyTDFunction from an InterpreterClass. If so,
          # then we must have aliased an imported function inside a class, so
          # we shouldn't bind the function to the class.
          if not isinstance(value, abstract.PyTDFunction) or not isinstance(
              base, abstract.InterpreterClass
          ):
            # See BaseValue.property_get for an explanation of the
            # parameters we're passing here.
            value = value.property_get(
                valself.AssignToNewVariable(node),
                abstract_utils.is_subclass(valself.data, cls),
            )
          if isinstance(value, abstract.Property):
            try:
              node, value = value.call(node, None, None)
            except error_types.FailedFunctionCall as error:
              # In normal circumstances, property calls should not fail. But in
              # case one ever does, handle the failure gracefully.
              self.ctx.errorlog.invalid_function_call(
                  self.ctx.vm.stack(value), error
              )
              value = self.ctx.new_unsolvable(node)
            final_values = value.data
          else:
            final_values = [value]
        else:
          final_values = [value]
        for final_value in final_values:
          ret.AddBinding(final_value, [varval] + add_origins, node)
      break  # we found a class which has this attribute
    return ret

  def _get_attribute_flat(self, node, cls, name, valself):
    """Flat attribute retrieval (no mro lookup)."""
    if isinstance(cls, abstract.ParameterizedClass):
      return self._get_attribute_flat(node, cls.base_cls, name, valself)
    elif isinstance(cls, abstract.Class):
      node, attr = self._get_member(node, cls, name, valself)
      if attr is not None:
        attr = self._filter_var(node, attr)
      return node, attr
    elif isinstance(cls, (abstract.Unknown, abstract.Unsolvable)):
      # The object doesn't have an MRO, so this is the same as get_attribute
      return self.get_attribute(node, cls, name)
    else:
      return node, None

  def _get_member(self, node, obj, name, valself):
    """Get a member of an object."""
    if isinstance(obj, mixin.LazyMembers):
      if not valself:
        subst = None
      elif isinstance(valself.data, abstract.Instance):
        # We need to rebind the parameter values at the root because that's the
        # node at which load_lazy_attribute() converts pyvals.
        subst = datatypes.AliasingDict(
            aliases=valself.data.instance_type_parameters.aliases
        )
        for k, v in valself.data.instance_type_parameters.items():
          if v.bindings:
            subst[k] = self.ctx.program.NewVariable(
                v.data, [], self.ctx.root_node
            )
          else:
            # An empty instance parameter means that the instance's class
            # inherits from a generic class without filling in parameter values:
            #   class Base(Generic[T]): ...
            #   class Child(Base): ...  # equivalent to `class Child(Base[Any])`
            # When this happens, parameter values are implicitly set to Any.
            subst[k] = self.ctx.new_unsolvable(self.ctx.root_node)
        subst[f"{obj.full_name}.Self"] = valself.AssignToNewVariable()
      elif isinstance(valself.data, abstract.Class):
        subst = {
            f"{obj.full_name}.Self": valself.data.instantiate(
                self.ctx.root_node
            )
        }
        if isinstance(valself.data, abstract.ParameterizedClass):
          for k, v in valself.data.formal_type_parameters.items():
            subst[f"{valself.data.full_name}.{k}"] = v.instantiate(node)
      else:
        subst = None
      member = obj.load_lazy_attribute(name, subst)
      if member:
        return node, member

    # If we are looking up a member that we can determine is an instance
    # rather than a class attribute, add it to the instance's members.
    if isinstance(obj, abstract.Instance):
      if name not in obj.members or not obj.members[name].bindings:
        # See test_generic.testInstanceAttributeVisible for an example of an
        # attribute in self.members needing to be reloaded.
        self._maybe_load_as_instance_attribute(node, obj, name)

    # Retrieve member
    if name in obj.members and obj.members[name].Bindings(node):
      # A retrieved attribute may be later mutated; we have no way of tracking
      # this. Forcibly clear obj's hash and type key caches so that attribute
      # changes are detected.
      obj.update_caches(force=True)
      return node, obj.members[name]
    return node, None

  def _filter_var(self, node, var):
    """Filter the variable by the node.

    Filters the variable data, including recursively expanded type parameter
    instances, by visibility at the node. A type parameter instance needs to be
    filtered at the moment of access because its value may change later.

    Args:
      node: The current node.
      var: A variable to filter.

    Returns:
      The filtered variable.
    """
    # First, check if we need to do any filtering at all. This method is
    # heavily called, so creating the `ret` variable judiciously reduces the
    # number of variables per pytype run by as much as 20%.
    bindings = var.Bindings(node) if len(var.bindings) > 1 else var.bindings
    if not bindings:
      return None
    if len(bindings) == len(var.bindings) and not any(
        isinstance(b.data, abstract.TypeParameterInstance) for b in bindings
    ):
      return var
    ret = self.ctx.program.NewVariable()
    for binding in bindings:
      val = binding.data
      if isinstance(val, abstract.TypeParameterInstance):
        var = val.instance.get_instance_type_parameter(val.name)
        # If this type parameter has visible values, we add those to the
        # return value. Otherwise, if it has constraints, we add those as an
        # upper bound on the values. When all else fails, we add an empty
        # value as a placeholder that can be passed around and converted to
        # Any after analysis.
        var_bindings = var.Bindings(node)
        if var_bindings:
          bindings.extend(var_bindings)
        elif val.param.constraints or val.param.bound:
          ret.PasteVariable(val.param.instantiate(node))
        else:
          ret.AddBinding(self.ctx.convert.empty, [], node)
      else:
        ret.AddBinding(val, {binding}, node)
    if ret.bindings:
      return ret
    else:
      return None

  def _maybe_load_as_instance_attribute(
      self, node: cfg.CFGNode, obj: abstract.SimpleValue, name: str
  ) -> None:
    if not isinstance(obj.cls, abstract.Class):
      return
    for base in obj.cls.mro:
      if isinstance(base, abstract.ParameterizedClass):
        base = base.base_cls
      if isinstance(base, abstract.PyTDClass):
        var = base.convert_as_instance_attribute(name, obj)
        if var is not None:
          if name in obj.members:
            obj.members[name].PasteVariable(var, node)
          else:
            obj.members[name] = var
          return

  def _set_member(
      self,
      node: cfg.CFGNode,
      obj: abstract.SimpleValue,
      name: str,
      var: cfg.Variable,
  ) -> cfg.CFGNode:
    """Set a member on an object."""
    if isinstance(obj, mixin.LazyMembers):
      obj.load_lazy_attribute(name)

    if name == "__class__":
      return obj.set_class(node, var)

    if (
        isinstance(obj, (abstract.PyTDFunction, abstract.SignedFunction))
        and name == "__defaults__"
    ):
      log.info("Setting defaults for %s to %r", obj.name, var)
      obj.set_function_defaults(node, var)
      return node

    def should_convert_to_func(v):
      return (
          not v.from_annotation
          and isinstance(v.cls, abstract.CallableClass)
          and v.cls.num_args >= 1
      )

    if isinstance(obj, abstract.Instance) and name not in obj.members:
      # The previous value needs to be loaded at the root node so that
      # (1) it is overwritten by the current value and (2) it is still
      # visible on branches where the current value is not
      self._maybe_load_as_instance_attribute(self.ctx.root_node, obj, name)
    elif (
        self.ctx.vm.frame.func
        and self.ctx.vm.frame.func.data.is_class_builder
        and obj is self.ctx.vm.frame.f_locals
        and any(should_convert_to_func(v) for v in var.data)
    ):
      # If we are setting a class attribute to a Callable whose type does not
      # come from a user-provided annotation, we convert the Callable to a
      # SimpleFunction, so "self" is accounted for correctly in bound and
      # unbound calls. We don't do this for user annotations because those
      # typically omit "self".
      var2 = self.ctx.program.NewVariable()
      for b in var.bindings:
        if not should_convert_to_func(b.data):
          var2.PasteBinding(b)
          continue
        sig = function.Signature.from_callable(b.data.cls)
        sig.name = name
        # Rename the first parameter to "self".
        self_name = sig.param_names[0]
        sig.set_annotation("self", sig.annotations[self_name])
        sig.del_annotation(self_name)
        sig.param_names = ("self",) + sig.param_names[1:]
        new_val = abstract.SimpleFunction(sig, self.ctx)
        var2.AddBinding(new_val, {b}, node)
      var = var2

    variable = obj.members.get(name)
    if variable:
      old_len = len(variable.bindings)
      variable.PasteVariable(var, node)
      log.debug(
          "Adding choice(s) to %s: %d new values (%d total)",
          name,
          len(variable.bindings) - old_len,
          len(variable.bindings),
      )
    else:
      log.debug(
          "Setting %s to the %d values in %r", name, len(var.bindings), var
      )
      variable = var.AssignToNewVariable(node)
      obj.members[name] = variable
    return node
