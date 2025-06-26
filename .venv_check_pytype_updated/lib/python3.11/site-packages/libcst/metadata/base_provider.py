# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from pathlib import Path
from types import MappingProxyType
from typing import (
    Generic,
    List,
    Mapping,
    MutableMapping,
    Optional,
    Protocol,
    Type,
    TYPE_CHECKING,
    TypeVar,
    Union,
)

from libcst._batched_visitor import BatchableCSTVisitor
from libcst._metadata_dependent import (
    _T as _MetadataT,
    _UNDEFINED_DEFAULT,
    LazyValue,
    MetadataDependent,
)
from libcst._visitors import CSTVisitor

if TYPE_CHECKING:
    from libcst._nodes.base import CSTNode
    from libcst._nodes.module import _ModuleSelfT as _ModuleT, Module
    from libcst.metadata.wrapper import MetadataWrapper


ProviderT = Type["BaseMetadataProvider[object]"]
# BaseMetadataProvider[int] would be a subtype of BaseMetadataProvider[object], so the
# typevar is covariant.
_ProvidedMetadataT = TypeVar("_ProvidedMetadataT", covariant=True)
MaybeLazyMetadataT = Union[LazyValue[_ProvidedMetadataT], _ProvidedMetadataT]


class GenCacheMethod(Protocol):
    def __call__(
        self,
        root_path: Path,
        paths: List[str],
        *,
        timeout: Optional[int] = None,
        use_pyproject_toml: bool = False,
    ) -> Mapping[str, object]: ...


# We can't use an ABCMeta here, because of metaclass conflicts
class BaseMetadataProvider(MetadataDependent, Generic[_ProvidedMetadataT]):
    """
    The low-level base class for all metadata providers. This class should be
    extended for metadata providers that are not visitor-based.

    This class is generic. A subclass of ``BaseMetadataProvider[T]`` will
    provider metadata of type ``T``.
    """

    #: Cache of metadata computed by this provider
    #
    # N.B. This has some typing variance problems. See `set_metadata` for an
    # explanation.
    _computed: MutableMapping["CSTNode", MaybeLazyMetadataT]

    #: Implement gen_cache to indicate the metadata provider depends on cache from external
    #: system. This function will be called by :class:`~libcst.metadata.FullRepoManager`
    #: to compute required cache object per file path.
    gen_cache: Optional[GenCacheMethod] = None

    def __init__(self, cache: object = None) -> None:
        super().__init__()
        self._computed: MutableMapping["CSTNode", MaybeLazyMetadataT] = {}
        if self.gen_cache and cache is None:
            # The metadata provider implementation is responsible to store and use cache.
            raise ValueError(
                f"Cache is required for initializing {self.__class__.__name__}."
            )
        self.cache = cache

    def _gen(
        self, wrapper: "MetadataWrapper"
    ) -> Mapping["CSTNode", MaybeLazyMetadataT]:
        """
        Resolves and returns metadata mapping for the module in ``wrapper``.

        This method is used by the metadata resolver and should not be called
        directly.
        """

        self._computed = {}
        # Resolve metadata dependencies for this provider
        with self.resolve(wrapper):
            self._gen_impl(wrapper.module)

        # Copy into a mapping proxy to ensure immutability
        return MappingProxyType(dict(self._computed))

    def _gen_impl(self, module: "Module") -> None:
        """
        Override this method with a metadata computation implementation.
        """
        ...

    def set_metadata(self, node: "CSTNode", value: MaybeLazyMetadataT) -> None:
        """
        Record a metadata value ``value`` for ``node``.
        """
        self._computed[node] = value

    def get_metadata(
        self,
        key: Type["BaseMetadataProvider[_MetadataT]"],
        node: "CSTNode",
        default: Union[
            MaybeLazyMetadataT, Type[_UNDEFINED_DEFAULT]
        ] = _UNDEFINED_DEFAULT,
    ) -> _MetadataT:
        """
        The same method as :func:`~libcst.MetadataDependent.get_metadata` except
        metadata is accessed from ``self._computed`` in addition to ``self.metadata``.
        See :func:`~libcst.MetadataDependent.get_metadata`.
        """
        if key is type(self):
            if default is not _UNDEFINED_DEFAULT:
                ret = self._computed.get(node, default)
            else:
                ret = self._computed[node]
            if isinstance(ret, LazyValue):
                return ret()
            return ret

        return super().get_metadata(key, node, default)


class VisitorMetadataProvider(CSTVisitor, BaseMetadataProvider[_ProvidedMetadataT]):
    """
    The low-level base class for all non-batchable visitor-based metadata
    providers. Inherits from :class:`~libcst.CSTVisitor`.

    This class is generic. A subclass of ``VisitorMetadataProvider[T]`` will
    provider metadata of type ``T``.
    """

    def _gen_impl(self, module: "_ModuleT") -> None:
        module.visit(self)


class BatchableMetadataProvider(
    BatchableCSTVisitor, BaseMetadataProvider[_ProvidedMetadataT]
):
    """
    The low-level base class for all batchable visitor-based metadata providers.
    Batchable providers should be preferred when possible as they are more
    efficient to run compared to non-batchable visitor-based providers.
    Inherits from :class:`~libcst.BatchableCSTVisitor`.

    This class is generic. A subclass of ``BatchableMetadataProvider[T]`` will
    provider metadata of type ``T``.
    """

    def _gen_impl(self, module: "Module") -> None:
        """
        Batchables providers are resolved through _gen_batchable] so no
        implementation should be provided in _gen_impl.
        """
        pass
