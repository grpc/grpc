# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import inspect
from abc import ABC
from contextlib import contextmanager
from typing import (
    Callable,
    cast,
    ClassVar,
    Collection,
    Generic,
    Iterator,
    Mapping,
    Type,
    TYPE_CHECKING,
    TypeVar,
    Union,
)

if TYPE_CHECKING:
    # Circular dependency for typing reasons only
    from libcst._nodes.base import CSTNode  # noqa: F401
    from libcst.metadata.base_provider import (  # noqa: F401
        BaseMetadataProvider,
        ProviderT,
    )
    from libcst.metadata.wrapper import MetadataWrapper  # noqa: F401


_T = TypeVar("_T")


class _UNDEFINED_DEFAULT:
    pass


class LazyValue(Generic[_T]):
    """
    The class for implementing a lazy metadata loading mechanism that improves the
    performance when retriving expensive metadata (e.g., qualified names). Providers
    including :class:`~libcst.metadata.QualifiedNameProvider` use this class to load
    the metadata of a certain node lazily when calling
    :func:`~libcst.MetadataDependent.get_metadata`.
    """

    def __init__(self, callable: Callable[[], _T]) -> None:
        self.callable = callable
        self.return_value: Union[_T, Type[_UNDEFINED_DEFAULT]] = _UNDEFINED_DEFAULT

    def __call__(self) -> _T:
        if self.return_value is _UNDEFINED_DEFAULT:
            self.return_value = self.callable()
        return cast(_T, self.return_value)


class MetadataDependent(ABC):
    """
    The low-level base class for all classes that declare required metadata
    dependencies. :class:`~libcst.CSTVisitor` and :class:`~libcst.CSTTransformer`
    extend this class.
    """

    #: A cached copy of metadata computed by :func:`~libcst.MetadataDependent.resolve`.
    #: Prefer using :func:`~libcst.MetadataDependent.get_metadata` over accessing
    #: this attribute directly.
    metadata: Mapping["ProviderT", Mapping["CSTNode", object]]

    #: The set of metadata dependencies declared by this class.
    METADATA_DEPENDENCIES: ClassVar[Collection["ProviderT"]] = ()

    def __init__(self) -> None:
        self.metadata = {}

    @classmethod
    def get_inherited_dependencies(cls) -> Collection["ProviderT"]:
        """
        Returns all metadata dependencies declared by classes in the MRO of ``cls``
        that subclass this class.

        Recursively searches the MRO of the subclass for metadata dependencies.
        """
        try:
            # pyre-fixme[16]: use a hidden attribute to cache the property
            return cls._INHERITED_METADATA_DEPENDENCIES_CACHE
        except AttributeError:
            dependencies = set()
            for c in inspect.getmro(cls):
                if issubclass(c, MetadataDependent):
                    dependencies.update(c.METADATA_DEPENDENCIES)
            # pyre-fixme[16]: use a hidden attribute to cache the property
            cls._INHERITED_METADATA_DEPENDENCIES_CACHE = frozenset(dependencies)
            return cls._INHERITED_METADATA_DEPENDENCIES_CACHE

    @contextmanager
    def resolve(self, wrapper: "MetadataWrapper") -> Iterator[None]:
        """
        Context manager that resolves all metadata dependencies declared by
        ``self`` (using :func:`~libcst.MetadataDependent.get_inherited_dependencies`)
        on ``wrapper`` and caches it on ``self`` for use with
        :func:`~libcst.MetadataDependent.get_metadata`.

        Upon exiting this context manager, the metadata cache on ``self`` is
        cleared.
        """
        self.metadata = wrapper.resolve_many(self.get_inherited_dependencies())
        yield
        self.metadata = {}

    def get_metadata(
        self,
        key: Type["BaseMetadataProvider[_T]"],
        node: "CSTNode",
        default: _T = _UNDEFINED_DEFAULT,
    ) -> _T:
        """
        Returns the metadata provided by the ``key`` if it is accessible from
        this visitor. Metadata is accessible in a subclass of this class if ``key``
        is declared as a dependency by any class in the MRO of this class.
        """
        if key not in self.get_inherited_dependencies():
            raise KeyError(
                f"{key.__name__} is not declared as a dependency in {type(self).__name__}.METADATA_DEPENDENCIES."
            )

        if key not in self.metadata:
            raise KeyError(
                f"{key.__name__} is a dependency, but not set; did you forget a MetadataWrapper?"
            )

        if default is not _UNDEFINED_DEFAULT:
            value = self.metadata[key].get(node, default)
        else:
            value = self.metadata[key][node]
        if isinstance(value, LazyValue):
            value = value()
        return cast(_T, value)
