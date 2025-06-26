# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import abc
import builtins
from collections import defaultdict
from contextlib import contextmanager, ExitStack
from dataclasses import dataclass
from enum import auto, Enum
from typing import (
    Collection,
    Dict,
    Iterator,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Set,
    Tuple,
    Type,
    Union,
)

import libcst as cst
from libcst import ensure_type
from libcst._add_slots import add_slots
from libcst.helpers import get_full_name_for_node
from libcst.metadata.base_provider import BatchableMetadataProvider
from libcst.metadata.expression_context_provider import (
    ExpressionContext,
    ExpressionContextProvider,
)

# Comprehensions are handled separately in _visit_comp_alike due to
# the complexity of the semantics
_ASSIGNMENT_LIKE_NODES = (
    cst.AnnAssign,
    cst.AsName,
    cst.Assign,
    cst.AugAssign,
    cst.ClassDef,
    cst.CompFor,
    cst.FunctionDef,
    cst.Global,
    cst.Import,
    cst.ImportFrom,
    cst.NamedExpr,
    cst.Nonlocal,
    cst.Parameters,
    cst.WithItem,
    cst.TypeVar,
    cst.TypeAlias,
    cst.TypeVarTuple,
    cst.ParamSpec,
)


@add_slots
@dataclass(frozen=False)
class Access:
    """
    An Access records an access of an assignment.

    .. note::
       This scope analysis only analyzes access via a :class:`~libcst.Name` or  a :class:`~libcst.Name`
       node embedded in other node like :class:`~libcst.Call` or :class:`~libcst.Attribute`.
       It doesn't support type annontation using :class:`~libcst.SimpleString` literal for forward
       references. E.g. in this example, the ``"Tree"`` isn't parsed as an access::

           class Tree:
               def __new__(cls) -> "Tree":
                   ...
    """

    #: The node of the access. A name is an access when the expression context is
    #: :attr:`ExpressionContext.LOAD`. This is usually the name node representing the
    #: access, except for: 1) dotted imports, when it might be the attribute that
    #: represents the most specific part of the imported symbol; and 2) string
    #: annotations, when it is the entire string literal
    node: Union[cst.Name, cst.Attribute, cst.BaseString]

    #: The scope of the access. Note that a access could be in a child scope of its
    #: assignment.
    scope: "Scope"

    is_annotation: bool

    is_type_hint: bool

    __assignments: Set["BaseAssignment"]
    __index: int

    def __init__(
        self, node: cst.Name, scope: "Scope", is_annotation: bool, is_type_hint: bool
    ) -> None:
        self.node = node
        self.scope = scope
        self.is_annotation = is_annotation
        self.is_type_hint = is_type_hint
        self.__assignments = set()
        self.__index = scope._assignment_count

    def __hash__(self) -> int:
        return id(self)

    @property
    def referents(self) -> Collection["BaseAssignment"]:
        """Return all assignments of the access."""
        return self.__assignments

    @property
    def _index(self) -> int:
        return self.__index

    def record_assignment(self, assignment: "BaseAssignment") -> None:
        if assignment.scope != self.scope or assignment._index < self.__index:
            self.__assignments.add(assignment)

    def record_assignments(self, name: str) -> None:
        assignments = self.scope._resolve_scope_for_access(name, self.scope)
        # filter out assignments that happened later than this access
        previous_assignments = {
            assignment
            for assignment in assignments
            if assignment.scope != self.scope or assignment._index < self.__index
        }
        if not previous_assignments and assignments and self.scope.parent != self.scope:
            previous_assignments = self.scope.parent._resolve_scope_for_access(
                name, self.scope
            )
        self.__assignments |= previous_assignments


class QualifiedNameSource(Enum):
    IMPORT = auto()
    BUILTIN = auto()
    LOCAL = auto()


@add_slots
@dataclass(frozen=True)
class QualifiedName:
    #: Qualified name, e.g. ``a.b.c`` or ``fn.<locals>.var``.
    name: str

    #: Source of the name, either :attr:`QualifiedNameSource.IMPORT`, :attr:`QualifiedNameSource.BUILTIN`
    #: or :attr:`QualifiedNameSource.LOCAL`.
    source: QualifiedNameSource


class BaseAssignment(abc.ABC):
    """Abstract base class of :class:`Assignment` and :class:`BuitinAssignment`."""

    #: The name of assignment.
    name: str

    #: The scope associates to assignment.
    scope: "Scope"
    __accesses: Set[Access]

    def __init__(self, name: str, scope: "Scope") -> None:
        self.name = name
        self.scope = scope
        self.__accesses = set()

    def record_access(self, access: Access) -> None:
        if access.scope != self.scope or self._index < access._index:
            self.__accesses.add(access)

    def record_accesses(self, accesses: Set[Access]) -> None:
        later_accesses = {
            access
            for access in accesses
            if access.scope != self.scope or self._index < access._index
        }
        self.__accesses |= later_accesses
        earlier_accesses = accesses - later_accesses
        if earlier_accesses and self.scope.parent != self.scope:
            # Accesses "earlier" than the relevant assignment should be attached
            # to assignments of the same name in the parent
            for shadowed_assignment in self.scope.parent[self.name]:
                shadowed_assignment.record_accesses(earlier_accesses)

    @property
    def references(self) -> Collection[Access]:
        """Return all accesses of the assignment."""
        # we don't want to publicly expose the mutable version of this
        return self.__accesses

    def __hash__(self) -> int:
        return id(self)

    @property
    def _index(self) -> int:
        """Return an integer that represents the order of assignments in `scope`"""
        return -1

    @abc.abstractmethod
    def get_qualified_names_for(self, full_name: str) -> Set[QualifiedName]: ...


class Assignment(BaseAssignment):
    """An assignment records the name, CSTNode and its accesses."""

    #: The node of assignment, it could be a :class:`~libcst.Import`, :class:`~libcst.ImportFrom`,
    #: :class:`~libcst.Name`, :class:`~libcst.FunctionDef`, or :class:`~libcst.ClassDef`.
    node: cst.CSTNode
    __index: int

    def __init__(
        self, name: str, scope: "Scope", node: cst.CSTNode, index: int
    ) -> None:
        self.node = node
        self.__index = index
        super().__init__(name, scope)

    @property
    def _index(self) -> int:
        return self.__index

    def get_qualified_names_for(self, full_name: str) -> Set[QualifiedName]:
        return {
            QualifiedName(
                (
                    f"{self.scope._name_prefix}.{full_name}"
                    if self.scope._name_prefix
                    else full_name
                ),
                QualifiedNameSource.LOCAL,
            )
        }


# even though we don't override the constructor.
class BuiltinAssignment(BaseAssignment):
    """
    A BuiltinAssignment represents an value provide by Python as a builtin, including
    `functions <https://docs.python.org/3/library/functions.html>`_,
    `constants <https://docs.python.org/3/library/constants.html>`_, and
    `types <https://docs.python.org/3/library/stdtypes.html>`_.
    """

    def get_qualified_names_for(self, full_name: str) -> Set[QualifiedName]:
        return {QualifiedName(f"builtins.{self.name}", QualifiedNameSource.BUILTIN)}


class ImportAssignment(Assignment):
    """An assignment records the import node and it's alias"""

    as_name: cst.CSTNode

    def __init__(
        self,
        name: str,
        scope: "Scope",
        node: cst.CSTNode,
        index: int,
        as_name: cst.CSTNode,
    ) -> None:
        super().__init__(name, scope, node, index)
        self.as_name = as_name

    def get_module_name_for_import(self) -> str:
        module = ""
        if isinstance(self.node, cst.ImportFrom):
            module_attr = self.node.module
            relative = self.node.relative
            if module_attr:
                module = get_full_name_for_node(module_attr) or ""
            if relative:
                module = "." * len(relative) + module
        return module

    def get_qualified_names_for(self, full_name: str) -> Set[QualifiedName]:
        module = self.get_module_name_for_import()
        results = set()
        assert isinstance(self.node, (cst.ImportFrom, cst.Import))
        import_names = self.node.names
        if not isinstance(import_names, cst.ImportStar):
            for name in import_names:
                real_name = get_full_name_for_node(name.name)
                if not real_name:
                    continue
                # real_name can contain `.` for dotted imports
                # for these we want to find the longest prefix that matches full_name
                parts = real_name.split(".")
                real_names = [".".join(parts[:i]) for i in range(len(parts), 0, -1)]
                for real_name in real_names:
                    as_name = real_name
                    if module and module.endswith("."):
                        # from . import a
                        # real_name should be ".a"
                        real_name = f"{module}{real_name}"
                    elif module:
                        real_name = f"{module}.{real_name}"
                    if name and name.asname:
                        eval_alias = name.evaluated_alias
                        if eval_alias is not None:
                            as_name = eval_alias
                    if full_name.startswith(as_name):
                        remaining_name = full_name.split(as_name, 1)[1]
                        if remaining_name and not remaining_name.startswith("."):
                            continue
                        remaining_name = remaining_name.lstrip(".")
                        results.add(
                            QualifiedName(
                                (
                                    f"{real_name}.{remaining_name}"
                                    if remaining_name
                                    else real_name
                                ),
                                QualifiedNameSource.IMPORT,
                            )
                        )
                        break
        return results


class Assignments:
    """A container to provide all assignments in a scope."""

    def __init__(self, assignments: Mapping[str, Collection[BaseAssignment]]) -> None:
        self._assignments = assignments

    def __iter__(self) -> Iterator[BaseAssignment]:
        """Iterate through all assignments by ``for i in scope.assignments``."""
        for assignments in self._assignments.values():
            for assignment in assignments:
                yield assignment

    def __getitem__(self, node: Union[str, cst.CSTNode]) -> Collection[BaseAssignment]:
        """Get assignments given a name str or :class:`~libcst.CSTNode` by ``scope.assignments[node]``"""
        name = get_full_name_for_node(node)
        return set(self._assignments[name]) if name in self._assignments else set()

    def __contains__(self, node: Union[str, cst.CSTNode]) -> bool:
        """Check if a name str or :class:`~libcst.CSTNode` has any assignment by ``node in scope.assignments``"""
        return len(self[node]) > 0


class Accesses:
    """A container to provide all accesses in a scope."""

    def __init__(self, accesses: Mapping[str, Collection[Access]]) -> None:
        self._accesses = accesses

    def __iter__(self) -> Iterator[Access]:
        """Iterate through all accesses by ``for i in scope.accesses``."""
        for accesses in self._accesses.values():
            for access in accesses:
                yield access

    def __getitem__(self, node: Union[str, cst.CSTNode]) -> Collection[Access]:
        """Get accesses given a name str or :class:`~libcst.CSTNode` by ``scope.accesses[node]``"""
        name = get_full_name_for_node(node)
        return self._accesses[name] if name in self._accesses else set()

    def __contains__(self, node: Union[str, cst.CSTNode]) -> bool:
        """Check if a name str or :class:`~libcst.CSTNode` has any access by ``node in scope.accesses``"""
        return len(self[node]) > 0


class Scope(abc.ABC):
    """
    Base class of all scope classes. Scope object stores assignments from imports,
    variable assignments, function definition or class definition.
    A scope has a parent scope which represents the inheritance relationship. That means
    an assignment in parent scope is viewable to the child scope and the child scope may
    overwrites the assignment by using the same name.

    Use ``name in scope`` to check whether a name is viewable in the scope.
    Use ``scope[name]`` to retrieve all viewable assignments in the scope.

    .. note::
       This scope analysis module only analyzes local variable names and it doesn't handle
       attribute names; for example, given ``a.b.c = 1``, local variable name ``a`` is recorded
       as an assignment instead of ``c`` or ``a.b.c``. To analyze the assignment/access of
       arbitrary object attributes, we leave the job to type inference metadata provider
       coming in the future.
    """

    #: Parent scope. Note the parent scope of a GlobalScope is itself.
    parent: "Scope"

    #: Refers to the GlobalScope.
    globals: "GlobalScope"
    _assignments: MutableMapping[str, Set[BaseAssignment]]
    _assignment_count: int
    _accesses_by_name: MutableMapping[str, Set[Access]]
    _accesses_by_node: MutableMapping[cst.CSTNode, Set[Access]]
    _name_prefix: str

    def __init__(self, parent: "Scope") -> None:
        super().__init__()
        self.parent = parent
        self.globals = parent.globals
        self._assignments = defaultdict(set)
        self._assignment_count = 0
        self._accesses_by_name = defaultdict(set)
        self._accesses_by_node = defaultdict(set)
        self._name_prefix = ""

    def record_assignment(self, name: str, node: cst.CSTNode) -> None:
        target = self._find_assignment_target(name)
        target._assignments[name].add(
            Assignment(
                name=name, scope=target, node=node, index=target._assignment_count
            )
        )

    def record_import_assignment(
        self, name: str, node: cst.CSTNode, as_name: cst.CSTNode
    ) -> None:
        target = self._find_assignment_target(name)
        target._assignments[name].add(
            ImportAssignment(
                name=name,
                scope=target,
                node=node,
                as_name=as_name,
                index=target._assignment_count,
            )
        )

    def _find_assignment_target(self, name: str) -> "Scope":
        return self

    def record_access(self, name: str, access: Access) -> None:
        self._accesses_by_name[name].add(access)
        self._accesses_by_node[access.node].add(access)

    def _is_visible_from_children(self, from_scope: "Scope") -> bool:
        """Returns if the assignments in this scope can be accessed from children.

        This is normally True, except for class scopes::

            def outer_fn():
                v = ...  # outer_fn's declaration
                class InnerCls:
                    v = ...  # shadows outer_fn's declaration
                    class InnerInnerCls:
                        v = ...  # shadows all previous declarations of v
                        def inner_fn():
                            nonlocal v
                            v = ...  # this refers to outer_fn's declaration
                                     # and not to any of the inner classes' as those are
                                     # hidden from their children.
        """
        return True

    def _next_visible_parent(
        self, from_scope: "Scope", first: Optional["Scope"] = None
    ) -> "Scope":
        parent = first if first is not None else self.parent
        while not parent._is_visible_from_children(from_scope):
            parent = parent.parent
        return parent

    @abc.abstractmethod
    def __contains__(self, name: str) -> bool:
        """Check if the name str exist in current scope by ``name in scope``."""
        ...

    def __getitem__(self, name: str) -> Set[BaseAssignment]:
        """
        Get assignments given a name str by ``scope[name]``.

        .. note::
           *Why does it return a list of assignments given a name instead of just one assignment?*

           Many programming languages differentiate variable declaration and assignment.
           Further, those programming languages often disallow duplicate declarations within
           the same scope, and will often hoist the declaration (without its assignment) to
           the top of the scope. These design decisions make static analysis much easier,
           because it's possible to match a name against its single declaration for a given scope.

           As an example, the following code would be valid in JavaScript::

               function fn() {
                 console.log(value);  // value is defined here, because the declaration is hoisted, but is currently 'undefined'.
                 var value = 5;  // A function-scoped declaration.
               }
               fn();  // prints 'undefined'.

           In contrast, Python's declaration and assignment are identical and are not hoisted::

               if conditional_value:
                   value = 5
               elif other_conditional_value:
                   value = 10
               print(value)  # possibly valid, depending on conditional execution

           This code may throw a ``NameError`` if both conditional values are falsy.
           It also means that depending on the codepath taken, the original declaration
           could come from either ``value = ...`` assignment node.
           As a result, instead of returning a single declaration,
           we're forced to return a collection of all of the assignments we think could have
           defined a given name by the time a piece of code is executed.
           For the above example, value would resolve to a set of both assignments.
        """
        return self._resolve_scope_for_access(name, self)

    @abc.abstractmethod
    def _resolve_scope_for_access(
        self, name: str, from_scope: "Scope"
    ) -> Set[BaseAssignment]: ...

    def __hash__(self) -> int:
        return id(self)

    @abc.abstractmethod
    def record_global_overwrite(self, name: str) -> None: ...

    @abc.abstractmethod
    def record_nonlocal_overwrite(self, name: str) -> None: ...

    def get_qualified_names_for(
        self, node: Union[str, cst.CSTNode]
    ) -> Collection[QualifiedName]:
        """Get all :class:`~libcst.metadata.QualifiedName` in current scope given a
        :class:`~libcst.CSTNode`.
        The source of a qualified name can be either :attr:`QualifiedNameSource.IMPORT`,
        :attr:`QualifiedNameSource.BUILTIN` or :attr:`QualifiedNameSource.LOCAL`.
        Given the following example, ``c`` has qualified name ``a.b.c`` with source ``IMPORT``,
        ``f`` has qualified name ``Cls.f`` with source ``LOCAL``, ``a`` has qualified name
        ``Cls.f.<locals>.a``, ``i`` has qualified name ``Cls.f.<locals>.<comprehension>.i``,
        and the builtin ``int`` has qualified name ``builtins.int`` with source ``BUILTIN``::

            from a.b import c
            class Cls:
                def f(self) -> "c":
                    c()
                    a = int("1")
                    [i for i in c()]

        We extends `PEP-3155 <https://www.python.org/dev/peps/pep-3155/>`_
        (defines ``__qualname__`` for class and function only; function namespace is followed
        by a ``<locals>``) to provide qualified name for all :class:`~libcst.CSTNode`
        recorded by :class:`~libcst.metadata.Assignment` and :class:`~libcst.metadata.Access`.
        The namespace of a comprehension (:class:`~libcst.ListComp`, :class:`~libcst.SetComp`,
        :class:`~libcst.DictComp`) is represented with ``<comprehension>``.

        An imported name may be used for type annotation with :class:`~libcst.SimpleString` and
        currently resolving the qualified given :class:`~libcst.SimpleString` is not supported
        considering it could be a complex type annotation in the string which is hard to
        resolve, e.g. ``List[Union[int, str]]``.
        """
        # if this node is an access we know the assignment and we can use that name
        node_accesses = (
            self._accesses_by_node.get(node) if isinstance(node, cst.CSTNode) else None
        )
        if node_accesses:
            return {
                qname
                for access in node_accesses
                for referent in access.referents
                for qname in referent.get_qualified_names_for(referent.name)
            }

        full_name = get_full_name_for_node(node)
        if full_name is None:
            return set()

        assignments = set()
        prefix = full_name
        while prefix:
            if prefix in self:
                assignments = self[prefix]
                break
            idx = prefix.rfind(".")
            prefix = None if idx == -1 else prefix[:idx]

        if not isinstance(node, str):
            for assignment in assignments:
                if isinstance(assignment, Assignment) and _is_assignment(
                    node, assignment.node
                ):
                    return assignment.get_qualified_names_for(full_name)

        results = set()
        for assignment in assignments:
            results |= assignment.get_qualified_names_for(full_name)
        return results

    @property
    def assignments(self) -> Assignments:
        """Return an :class:`~libcst.metadata.Assignments` contains all assignmens in current scope."""
        return Assignments(self._assignments)

    @property
    def accesses(self) -> Accesses:
        """Return an :class:`~libcst.metadata.Accesses` contains all accesses in current scope."""
        return Accesses(self._accesses_by_name)


class BuiltinScope(Scope):
    """
    A BuiltinScope represents python builtin declarations. See https://docs.python.org/3/library/builtins.html
    """

    def __init__(self, globals: Scope) -> None:
        self.globals: Scope = globals  # must be defined before Scope.__init__ is called
        super().__init__(parent=self)

    def __contains__(self, name: str) -> bool:
        return hasattr(builtins, name)

    def _resolve_scope_for_access(
        self, name: str, from_scope: "Scope"
    ) -> Set[BaseAssignment]:
        if name in self._assignments:
            return self._assignments[name]
        if hasattr(builtins, name):
            # note - we only see the builtin assignments during the deferred
            # access resolution. unfortunately that means we have to create the
            # assignment here, which can cause the set to mutate during iteration
            self._assignments[name].add(BuiltinAssignment(name, self))
            return self._assignments[name]
        return set()

    def record_global_overwrite(self, name: str) -> None:
        raise NotImplementedError("global overwrite in builtin scope are not allowed")

    def record_nonlocal_overwrite(self, name: str) -> None:
        raise NotImplementedError("declarations in builtin scope are not allowed")

    def _find_assignment_target(self, name: str) -> "Scope":
        raise NotImplementedError("assignments in builtin scope are not allowed")


class GlobalScope(Scope):
    """
    A GlobalScope is the scope of module. All module level assignments are recorded in GlobalScope.
    """

    def __init__(self) -> None:
        super().__init__(parent=BuiltinScope(self))

    def __contains__(self, name: str) -> bool:
        if name in self._assignments:
            return len(self._assignments[name]) > 0
        return name in self._next_visible_parent(self)

    def _resolve_scope_for_access(
        self, name: str, from_scope: "Scope"
    ) -> Set[BaseAssignment]:
        if name in self._assignments:
            return self._assignments[name]

        parent = self._next_visible_parent(from_scope)
        return parent[name]

    def record_global_overwrite(self, name: str) -> None:
        pass

    def record_nonlocal_overwrite(self, name: str) -> None:
        raise NotImplementedError("nonlocal declaration not allowed at module level")


class LocalScope(Scope, abc.ABC):
    _scope_overwrites: Dict[str, Scope]

    #: Name of function. Used as qualified name.
    name: Optional[str]

    #: The :class:`~libcst.CSTNode` node defines the current scope.
    node: cst.CSTNode

    def __init__(
        self, parent: Scope, node: cst.CSTNode, name: Optional[str] = None
    ) -> None:
        super().__init__(parent)
        self.name = name
        self.node = node
        self._scope_overwrites = {}
        # pyre-fixme[4]: Attribute `_name_prefix` of class `LocalScope` has type `str` but no type is specified.
        self._name_prefix = self._make_name_prefix()

    def record_global_overwrite(self, name: str) -> None:
        self._scope_overwrites[name] = self.globals

    def record_nonlocal_overwrite(self, name: str) -> None:
        self._scope_overwrites[name] = self.parent

    def _find_assignment_target(self, name: str) -> "Scope":
        if name in self._scope_overwrites:
            scope = self._scope_overwrites[name]
            return self._next_visible_parent(self, scope)._find_assignment_target(name)
        else:
            return super()._find_assignment_target(name)

    def __contains__(self, name: str) -> bool:
        if name in self._scope_overwrites:
            return name in self._scope_overwrites[name]
        if name in self._assignments:
            return len(self._assignments[name]) > 0
        return name in self._next_visible_parent(self)

    def _resolve_scope_for_access(
        self, name: str, from_scope: "Scope"
    ) -> Set[BaseAssignment]:
        if name in self._scope_overwrites:
            scope = self._scope_overwrites[name]
            return self._next_visible_parent(
                from_scope, scope
            )._resolve_scope_for_access(name, from_scope)
        if name in self._assignments:
            return self._assignments[name]
        else:
            return self._next_visible_parent(from_scope)._resolve_scope_for_access(
                name, from_scope
            )

    def _make_name_prefix(self) -> str:
        # filter falsey strings out
        return ".".join(filter(None, [self.parent._name_prefix, self.name, "<locals>"]))


# even though we don't override the constructor.
class FunctionScope(LocalScope):
    """
    When a function is defined, it creates a FunctionScope.
    """

    pass


# even though we don't override the constructor.
class ClassScope(LocalScope):
    """
    When a class is defined, it creates a ClassScope.
    """

    def _is_visible_from_children(self, from_scope: "Scope") -> bool:
        return from_scope.parent is self and isinstance(from_scope, AnnotationScope)

    def _make_name_prefix(self) -> str:
        # filter falsey strings out
        return ".".join(filter(None, [self.parent._name_prefix, self.name]))


# even though we don't override the constructor.
class ComprehensionScope(LocalScope):
    """
    Comprehensions and generator expressions create their own scope. For example, in

        [i for i in range(10)]

    The variable ``i`` is only viewable within the ComprehensionScope.
    """

    # TODO: Assignment expressions (Python 3.8) will complicate ComprehensionScopes,
    # and will require us to handle such assignments as non-local.
    # https://www.python.org/dev/peps/pep-0572/#scope-of-the-target

    def _make_name_prefix(self) -> str:
        # filter falsey strings out
        return ".".join(filter(None, [self.parent._name_prefix, "<comprehension>"]))


class AnnotationScope(LocalScope):
    """
    Scopes used for type aliases and type parameters as defined by PEP-695.

    These scopes are created for type parameters using the special syntax, as well as
    type aliases. See https://peps.python.org/pep-0695/#scoping-behavior for more.
    """

    def _make_name_prefix(self) -> str:
        # these scopes are transparent for the purposes of qualified names
        return self.parent._name_prefix


# Generates dotted names from an Attribute or Name node:
# Attribute(value=Name(value="a"), attr=Name(value="b")) -> ("a.b", "a")
# each string has the corresponding CSTNode attached to it
def _gen_dotted_names(
    node: Union[cst.Attribute, cst.Name],
) -> Iterator[Tuple[str, Union[cst.Attribute, cst.Name]]]:
    if isinstance(node, cst.Name):
        yield node.value, node
    else:
        value = node.value
        if isinstance(value, cst.Call):
            value = value.func
            if isinstance(value, (cst.Attribute, cst.Name)):
                name_values = _gen_dotted_names(value)
                try:
                    next_name, next_node = next(name_values)
                except StopIteration:
                    return
                else:
                    yield next_name, next_node
                    yield from name_values
        elif isinstance(value, (cst.Attribute, cst.Name)):
            name_values = _gen_dotted_names(value)
            try:
                next_name, next_node = next(name_values)
            except StopIteration:
                return
            else:
                yield f"{next_name}.{node.attr.value}", node
                yield next_name, next_node
                yield from name_values


def _is_assignment(node: cst.CSTNode, assignment_node: cst.CSTNode) -> bool:
    """
    Returns true if ``node`` is part of the assignment at ``assignment_node``.

    Normally this is just a simple identity check, except for imports where the
    assignment is attached to the entire import statement but we are interested in
    ``Name`` nodes inside the statement.
    """
    if node is assignment_node:
        return True
    if isinstance(assignment_node, (cst.Import, cst.ImportFrom)):
        aliases = assignment_node.names
        if isinstance(aliases, cst.ImportStar):
            return False
        for alias in aliases:
            if alias.name is node:
                return True
            asname = alias.asname
            if asname is not None:
                if asname.name is node:
                    return True
    return False


@dataclass(frozen=True)
class DeferredAccess:
    access: Access
    enclosing_attribute: Optional[cst.Attribute]
    enclosing_string_annotation: Optional[cst.BaseString]


class ScopeVisitor(cst.CSTVisitor):
    # since it's probably not useful. That can makes this visitor cleaner.
    def __init__(self, provider: "ScopeProvider") -> None:
        super().__init__()
        self.provider: ScopeProvider = provider
        self.scope: Scope = GlobalScope()
        self.__deferred_accesses: List[DeferredAccess] = []
        self.__top_level_attribute_stack: List[Optional[cst.Attribute]] = [None]
        self.__in_annotation_stack: List[bool] = [False]
        self.__in_type_hint_stack: List[bool] = [False]
        self.__in_ignored_subscript: Set[cst.Subscript] = set()
        self.__last_string_annotation: Optional[cst.BaseString] = None
        self.__ignore_annotation: int = 0

    @contextmanager
    def _new_scope(
        self, kind: Type[LocalScope], node: cst.CSTNode, name: Optional[str] = None
    ) -> Iterator[None]:
        parent_scope = self.scope
        self.scope = kind(parent_scope, node, name)
        try:
            yield
        finally:
            self.scope = parent_scope

    @contextmanager
    def _switch_scope(self, scope: Scope) -> Iterator[None]:
        current_scope = self.scope
        self.scope = scope
        try:
            yield
        finally:
            self.scope = current_scope

    def _visit_import_alike(self, node: Union[cst.Import, cst.ImportFrom]) -> bool:
        names = node.names
        if isinstance(names, cst.ImportStar):
            return False

        # make sure node.names is Sequence[ImportAlias]
        for name in names:
            self.provider.set_metadata(name, self.scope)
            asname = name.asname
            if asname is not None:
                name_values = _gen_dotted_names(cst.ensure_type(asname.name, cst.Name))
                import_node_asname = asname.name
            else:
                name_values = _gen_dotted_names(name.name)
                import_node_asname = name.name

            for name_value, _ in name_values:
                self.scope.record_import_assignment(
                    name_value, node, import_node_asname
                )
        return False

    def visit_Import(self, node: cst.Import) -> Optional[bool]:
        return self._visit_import_alike(node)

    def visit_ImportFrom(self, node: cst.ImportFrom) -> Optional[bool]:
        return self._visit_import_alike(node)

    def visit_Attribute(self, node: cst.Attribute) -> Optional[bool]:
        if self.__top_level_attribute_stack[-1] is None:
            self.__top_level_attribute_stack[-1] = node
        node.value.visit(self)  # explicitly not visiting attr
        if self.__top_level_attribute_stack[-1] is node:
            self.__top_level_attribute_stack[-1] = None
        return False

    def visit_Call(self, node: cst.Call) -> Optional[bool]:
        self.__top_level_attribute_stack.append(None)
        self.__in_type_hint_stack.append(False)
        qnames = {qn.name for qn in self.scope.get_qualified_names_for(node)}
        if "typing.NewType" in qnames or "typing.TypeVar" in qnames:
            node.func.visit(self)
            self.__in_type_hint_stack[-1] = True
            for arg in node.args[1:]:
                arg.visit(self)
            return False
        if "typing.cast" in qnames:
            node.func.visit(self)
            if len(node.args) > 0:
                self.__in_type_hint_stack.append(True)
                node.args[0].visit(self)
                self.__in_type_hint_stack.pop()
                for arg in node.args[1:]:
                    arg.visit(self)
            return False
        return True

    def leave_Call(self, original_node: cst.Call) -> None:
        self.__top_level_attribute_stack.pop()
        self.__in_type_hint_stack.pop()

    def visit_Annotation(self, node: cst.Annotation) -> Optional[bool]:
        self.__in_annotation_stack.append(True)

    def leave_Annotation(self, original_node: cst.Annotation) -> None:
        self.__in_annotation_stack.pop()

    def visit_SimpleString(self, node: cst.SimpleString) -> Optional[bool]:
        self._handle_string_annotation(node)
        return False

    def visit_ConcatenatedString(self, node: cst.ConcatenatedString) -> Optional[bool]:
        return not self._handle_string_annotation(node)

    def _handle_string_annotation(
        self, node: Union[cst.SimpleString, cst.ConcatenatedString]
    ) -> bool:
        """Returns whether it successfully handled the string annotation"""
        if (
            self.__in_type_hint_stack[-1] or self.__in_annotation_stack[-1]
        ) and not self.__in_ignored_subscript:
            value = node.evaluated_value
            if value:
                top_level_annotation = self.__last_string_annotation is None
                if top_level_annotation:
                    self.__last_string_annotation = node
                try:
                    mod = cst.parse_module(value)
                    mod.visit(self)
                except cst.ParserSyntaxError:
                    # swallow string annotation parsing errors
                    # this is the same behavior as cPython
                    pass
                if top_level_annotation:
                    self.__last_string_annotation = None
                return True
        return False

    def visit_Subscript(self, node: cst.Subscript) -> Optional[bool]:
        in_type_hint = False
        if isinstance(node.value, cst.Name):
            qnames = {qn.name for qn in self.scope.get_qualified_names_for(node.value)}
            if any(qn.startswith(("typing.", "typing_extensions.")) for qn in qnames):
                in_type_hint = True
            if "typing.Literal" in qnames or "typing_extensions.Literal" in qnames:
                self.__in_ignored_subscript.add(node)

        self.__in_type_hint_stack.append(in_type_hint)
        return True

    def leave_Subscript(self, original_node: cst.Subscript) -> None:
        self.__in_type_hint_stack.pop()
        self.__in_ignored_subscript.discard(original_node)

    def visit_Name(self, node: cst.Name) -> Optional[bool]:
        # not all Name have ExpressionContext
        context = self.provider.get_metadata(ExpressionContextProvider, node, None)
        if context == ExpressionContext.STORE:
            self.scope.record_assignment(node.value, node)
        elif context in (ExpressionContext.LOAD, ExpressionContext.DEL, None):
            access = Access(
                node,
                self.scope,
                is_annotation=bool(
                    self.__in_annotation_stack[-1] and not self.__ignore_annotation
                ),
                is_type_hint=bool(self.__in_type_hint_stack[-1]),
            )
            self.__deferred_accesses.append(
                DeferredAccess(
                    access=access,
                    enclosing_attribute=self.__top_level_attribute_stack[-1],
                    enclosing_string_annotation=self.__last_string_annotation,
                )
            )

    def visit_FunctionDef(self, node: cst.FunctionDef) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)
        self.provider.set_metadata(node.name, self.scope)

        with ExitStack() as stack:
            if node.type_parameters:
                stack.enter_context(self._new_scope(AnnotationScope, node, None))
                node.type_parameters.visit(self)

            with self._new_scope(
                FunctionScope, node, get_full_name_for_node(node.name)
            ):
                node.params.visit(self)
                node.body.visit(self)

            for decorator in node.decorators:
                decorator.visit(self)
            returns = node.returns
            if returns:
                returns.visit(self)

        return False

    def visit_Lambda(self, node: cst.Lambda) -> Optional[bool]:
        with self._new_scope(FunctionScope, node):
            node.params.visit(self)
            node.body.visit(self)
        return False

    def visit_Param(self, node: cst.Param) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)
        self.provider.set_metadata(node.name, self.scope)
        with self._switch_scope(self.scope.parent):
            for field in [node.default, node.annotation]:
                if field:
                    field.visit(self)

        return False

    def visit_Arg(self, node: cst.Arg) -> bool:
        # The keyword of Arg is neither an Assignment nor an Access and we explicitly don't visit it.
        value = node.value
        if value:
            value.visit(self)
        return False

    def visit_ClassDef(self, node: cst.ClassDef) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)
        self.provider.set_metadata(node.name, self.scope)
        for decorator in node.decorators:
            decorator.visit(self)

        with ExitStack() as stack:
            if node.type_parameters:
                stack.enter_context(self._new_scope(AnnotationScope, node, None))
                node.type_parameters.visit(self)

            for base in node.bases:
                base.visit(self)
            for keyword in node.keywords:
                keyword.visit(self)

            with self._new_scope(ClassScope, node, get_full_name_for_node(node.name)):
                for statement in node.body.body:
                    statement.visit(self)
        return False

    def visit_ClassDef_bases(self, node: cst.ClassDef) -> None:
        self.__ignore_annotation += 1

    def leave_ClassDef_bases(self, node: cst.ClassDef) -> None:
        self.__ignore_annotation -= 1

    def visit_Global(self, node: cst.Global) -> Optional[bool]:
        for name_item in node.names:
            self.scope.record_global_overwrite(name_item.name.value)
        return False

    def visit_Nonlocal(self, node: cst.Nonlocal) -> Optional[bool]:
        for name_item in node.names:
            self.scope.record_nonlocal_overwrite(name_item.name.value)
        return False

    def visit_ListComp(self, node: cst.ListComp) -> Optional[bool]:
        return self._visit_comp_alike(node)

    def visit_SetComp(self, node: cst.SetComp) -> Optional[bool]:
        return self._visit_comp_alike(node)

    def visit_DictComp(self, node: cst.DictComp) -> Optional[bool]:
        return self._visit_comp_alike(node)

    def visit_GeneratorExp(self, node: cst.GeneratorExp) -> Optional[bool]:
        return self._visit_comp_alike(node)

    def _visit_comp_alike(
        self, node: Union[cst.ListComp, cst.SetComp, cst.DictComp, cst.GeneratorExp]
    ) -> bool:
        """
        Cheat sheet: `[elt for target in iter if ifs]`

        Terminology:
            target: The variable or pattern we're storing each element of the iter in.
            iter: The thing we're iterating over.
            ifs: A list of conditions provided
            elt: The value that will be computed and "yielded" each time the loop
                iterates. For most comprehensions, this is just the `node.elt`, but
                DictComp has `key` and `value`, which behave like `node.elt` would.


        Nested Comprehension: ``[a for b in c for a in b]`` is a "nested" ListComp.
        The outer iterator is in ``node.for_in`` and the inner iterator is in
        ``node.for_in.inner_for_in``.


        The first comprehension object's iter in generators is evaluated
        outside of the ComprehensionScope. Every other comprehension's iter is
        evaluated inside the ComprehensionScope. Even though that doesn't seem very sane,
        but that appears to be how it works.

            non_flat = [ [1,2,3], [4,5,6], [7,8]
            flat = [y for x in non_flat for y in x]  # this works fine

            # This will give a "NameError: name 'x' is not defined":
            flat = [y for x in x for y in x]
            # x isn't defined, because the first iter is evaluted outside the scope.

            # This will give an UnboundLocalError, indicating that the second
            # comprehension's iter value is evaluated inside the scope as its elt.
            # UnboundLocalError: local variable 'y' referenced before assignment
            flat = [y for x in non_flat for y in y]
        """
        for_in = node.for_in
        for_in.iter.visit(self)
        self.provider.set_metadata(for_in, self.scope)
        with self._new_scope(ComprehensionScope, node):
            for_in.target.visit(self)
            # Things from here on can refer to the target.
            self.scope._assignment_count += 1
            for condition in for_in.ifs:
                condition.visit(self)
            inner_for_in = for_in.inner_for_in
            if inner_for_in:
                inner_for_in.visit(self)
            if isinstance(node, cst.DictComp):
                node.key.visit(self)
                node.value.visit(self)
            else:
                node.elt.visit(self)
        return False

    def visit_For(self, node: cst.For) -> Optional[bool]:
        node.target.visit(self)
        self.scope._assignment_count += 1
        for child in [node.iter, node.body, node.orelse, node.asynchronous]:
            if child is not None:
                child.visit(self)
        return False

    def infer_accesses(self) -> None:
        # Aggregate access with the same name and batch add with set union as an optimization.
        # In worst case, all accesses (m) and assignments (n) refer to the same name,
        # the time complexity is O(m x n), this optimizes it as O(m + n).
        scope_name_accesses = defaultdict(set)
        for def_access in self.__deferred_accesses:
            access, enclosing_attribute, enclosing_string_annotation = (
                def_access.access,
                def_access.enclosing_attribute,
                def_access.enclosing_string_annotation,
            )
            name = ensure_type(access.node, cst.Name).value
            if enclosing_attribute is not None:
                # if _gen_dotted_names doesn't generate any values, fall back to
                # the original name node above
                for attr_name, node in _gen_dotted_names(enclosing_attribute):
                    if attr_name in access.scope:
                        access.node = node
                        name = attr_name
                        break

            if enclosing_string_annotation is not None:
                access.node = enclosing_string_annotation

            scope_name_accesses[(access.scope, name)].add(access)
            access.record_assignments(name)
            access.scope.record_access(name, access)

        for (scope, name), accesses in scope_name_accesses.items():
            for assignment in scope._resolve_scope_for_access(name, scope):
                assignment.record_accesses(accesses)

        self.__deferred_accesses = []

    def on_leave(self, original_node: cst.CSTNode) -> None:
        self.provider.set_metadata(original_node, self.scope)
        if isinstance(original_node, _ASSIGNMENT_LIKE_NODES):
            self.scope._assignment_count += 1
        super().on_leave(original_node)

    def visit_TypeAlias(self, node: cst.TypeAlias) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)

        with self._new_scope(AnnotationScope, node, None):
            if node.type_parameters is not None:
                node.type_parameters.visit(self)
            node.value.visit(self)

        return False

    def visit_TypeVar(self, node: cst.TypeVar) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)

        if node.bound is not None:
            node.bound.visit(self)

        return False

    def visit_TypeVarTuple(self, node: cst.TypeVarTuple) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)
        return False

    def visit_ParamSpec(self, node: cst.ParamSpec) -> Optional[bool]:
        self.scope.record_assignment(node.name.value, node)
        return False


class ScopeProvider(BatchableMetadataProvider[Optional[Scope]]):
    """
    :class:`ScopeProvider` traverses the entire module and creates the scope inheritance
    structure. It provides the scope of name assignment and accesses. It is useful for
    more advanced static analysis. E.g. given a :class:`~libcst.FunctionDef`
    node, we can check the type of its Scope to figure out whether it is a class method
    (:class:`ClassScope`) or a regular function (:class:`GlobalScope`).

    Scope metadata is available for most node types other than formatting information nodes
    (whitespace, parentheses, etc.).
    """

    METADATA_DEPENDENCIES = (ExpressionContextProvider,)

    def visit_Module(self, node: cst.Module) -> Optional[bool]:
        visitor = ScopeVisitor(self)
        node.visit(visitor)
        visitor.infer_accesses()
