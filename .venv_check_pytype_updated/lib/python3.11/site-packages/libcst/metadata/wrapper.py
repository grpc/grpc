# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

import textwrap
from contextlib import ExitStack
from types import MappingProxyType
from typing import (
    Any,
    cast,
    Collection,
    Iterable,
    Mapping,
    MutableMapping,
    MutableSet,
    Optional,
    Type,
    TYPE_CHECKING,
    TypeVar,
)

from libcst._batched_visitor import BatchableCSTVisitor, visit_batched, VisitorMethod
from libcst._exceptions import MetadataException
from libcst.metadata.base_provider import BatchableMetadataProvider

if TYPE_CHECKING:
    from libcst._nodes.base import CSTNode  # noqa: F401
    from libcst._nodes.module import Module  # noqa: F401
    from libcst._visitors import CSTVisitorT  # noqa: F401
    from libcst.metadata.base_provider import (  # noqa: F401
        BaseMetadataProvider,
        ProviderT,
    )


_T = TypeVar("_T")


def _gen_batchable(
    wrapper: "MetadataWrapper",
    # pyre-fixme[2]: Parameter `providers` must have a type that does not contain `Any`
    providers: Iterable[BatchableMetadataProvider[Any]],
) -> Mapping["ProviderT", Mapping["CSTNode", object]]:
    """
    Returns map of metadata mappings from resolving ``providers`` on ``wrapper``.
    """
    wrapper.visit_batched(providers)

    # Make immutable metadata mapping
    # pyre-ignore[7]
    return {type(p): MappingProxyType(dict(p._computed)) for p in providers}


def _gather_providers(
    providers: Collection["ProviderT"], gathered: MutableSet["ProviderT"]
) -> MutableSet["ProviderT"]:
    """
    Recursively gathers all the given providers and their dependencies.
    """
    for P in providers:
        if P not in gathered:
            gathered.add(P)
            _gather_providers(P.METADATA_DEPENDENCIES, gathered)
    return gathered


def _resolve_impl(
    wrapper: "MetadataWrapper", providers: Collection["ProviderT"]
) -> None:
    """
    Updates the _metadata map on wrapper with metadata from the given providers
    as well as their dependencies.
    """
    completed = set(wrapper._metadata.keys())
    remaining = _gather_providers(set(providers), set()) - completed

    while len(remaining) > 0:
        batchable = set()

        for P in remaining:
            if set(P.METADATA_DEPENDENCIES).issubset(completed):
                if issubclass(P, BatchableMetadataProvider):
                    batchable.add(P)
                else:
                    wrapper._metadata[P] = (
                        P(wrapper._cache.get(P))._gen(wrapper)
                        if P.gen_cache
                        else P()._gen(wrapper)
                    )
                    completed.add(P)

        initialized_batchable = [
            p(wrapper._cache.get(p)) if p.gen_cache else p() for p in batchable
        ]
        metadata_batch = _gen_batchable(wrapper, initialized_batchable)
        wrapper._metadata.update(metadata_batch)
        completed |= batchable

        if len(completed) == 0 and len(batchable) == 0:
            # remaining must be non-empty at this point
            names = ", ".join([P.__name__ for P in remaining])
            raise MetadataException(f"Detected circular dependencies in {names}")

        remaining -= completed


class MetadataWrapper:
    """
    A wrapper around a :class:`~libcst.Module` that stores associated metadata
    for that module.

    When a :class:`MetadataWrapper` is constructed over a module, the wrapper will
    store a deep copy of the original module. This means
    ``MetadataWrapper(module).module == module`` is ``False``.

    This copying operation ensures that a node will never appear twice (by identity) in
    the same tree. This allows us to uniquely look up metadata for a node based on a
    node's identity.
    """

    __slots__ = ["__module", "_metadata", "_cache"]

    __module: "Module"
    _metadata: MutableMapping["ProviderT", Mapping["CSTNode", object]]
    _cache: Mapping["ProviderT", object]

    def __init__(
        self,
        module: "Module",
        unsafe_skip_copy: bool = False,
        cache: Mapping["ProviderT", object] = {},
    ) -> None:
        """
        :param module: The module to wrap. This is deeply copied by default.
        :param unsafe_skip_copy: When true, this skips the deep cloning of the module.
            This can provide a small performance benefit, but you should only use this
            if you know that there are no duplicate nodes in your tree (e.g. this
            module came from the parser).
        :param cache: Pass the needed cache to wrapper to be used when resolving metadata.
        """
        # Ensure that module is safe to use by copying the module to remove
        # any duplicate nodes.
        if not unsafe_skip_copy:
            module = module.deep_clone()
        self.__module = module
        self._metadata = {}
        self._cache = cache

    def __repr__(self) -> str:
        return f"MetadataWrapper(\n{textwrap.indent(repr(self.module), ' ' * 4)},\n)"

    @property
    def module(self) -> "Module":
        """
        The module that's wrapped by this MetadataWrapper. By default, this is a deep
        copy of the passed in module.

        ::

            mw = ModuleWrapper(module)
            # Because `mw.module is not module`, you probably want to do visit and do
            # your analysis on `mw.module`, not `module`.
            mw.module.visit(DoSomeAnalysisVisitor)
        """
        # use a property getter to enforce that this is a read-only variable
        return self.__module

    def resolve(
        self, provider: Type["BaseMetadataProvider[_T]"]
    ) -> Mapping["CSTNode", _T]:
        """
        Returns a copy of the metadata mapping computed by ``provider``.
        """
        if provider in self._metadata:
            metadata = self._metadata[provider]
        else:
            metadata = self.resolve_many([provider])[provider]

        return cast(Mapping["CSTNode", _T], metadata)

    def resolve_many(
        self, providers: Collection["ProviderT"]
    ) -> Mapping["ProviderT", Mapping["CSTNode", object]]:
        """
        Returns a copy of the map of metadata mapping computed by each provider
        in ``providers``.

        The returned map does not contain any metadata from undeclared metadata
        dependencies that ``providers`` has.
        """
        _resolve_impl(self, providers)

        # Only return what what declared in providers
        return {k: self._metadata[k] for k in providers}

    def visit(self, visitor: "CSTVisitorT") -> "Module":
        """
        Convenience method to resolve metadata before performing a traversal over
        ``self.module`` with ``visitor``. See :func:`~libcst.Module.visit`.
        """
        with visitor.resolve(self):
            return self.module.visit(visitor)

    def visit_batched(
        self,
        visitors: Iterable[BatchableCSTVisitor],
        before_visit: Optional[VisitorMethod] = None,
        after_leave: Optional[VisitorMethod] = None,
    ) -> "CSTNode":
        """
        Convenience method to resolve metadata before performing a traversal over
        ``self.module`` with ``visitors``. See :func:`~libcst.visit_batched`.
        """
        with ExitStack() as stack:
            # Resolve dependencies of visitors
            for v in visitors:
                stack.enter_context(v.resolve(self))

            return visit_batched(self.module, visitors, before_visit, after_leave)
