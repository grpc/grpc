# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import dataclasses
from pathlib import Path
from typing import Any, Collection, List, Mapping, Optional, Union

import libcst as cst
from libcst._metadata_dependent import LazyValue, MetadataDependent
from libcst.helpers.module import calculate_module_and_package, ModuleNameAndPackage
from libcst.metadata.base_provider import BatchableMetadataProvider
from libcst.metadata.scope_provider import (
    QualifiedName,
    QualifiedNameSource,
    ScopeProvider,
)


class QualifiedNameProvider(BatchableMetadataProvider[Collection[QualifiedName]]):
    """
    Compute possible qualified names of a variable CSTNode
    (extends `PEP-3155 <https://www.python.org/dev/peps/pep-3155/>`_).
    It uses the
    :func:`~libcst.metadata.Scope.get_qualified_names_for` underlying to get qualified names.
    Multiple qualified names may be returned, such as when we have conditional imports or an
    import shadows another. E.g., the provider finds ``a.b``, ``d.e`` and
    ``f.g`` as possible qualified names of ``c``::

        >>> wrapper = MetadataWrapper(
        >>>     cst.parse_module(dedent(
        >>>     '''
        >>>         if something:
        >>>             from a import b as c
        >>>         elif otherthing:
        >>>             from d import e as c
        >>>         else:
        >>>             from f import g as c
        >>>         c()
        >>>     '''
        >>>     ))
        >>> )
        >>> call = wrapper.module.body[1].body[0].value
        >>> wrapper.resolve(QualifiedNameProvider)[call],
        {
            QualifiedName(name="a.b", source=QualifiedNameSource.IMPORT),
            QualifiedName(name="d.e", source=QualifiedNameSource.IMPORT),
            QualifiedName(name="f.g", source=QualifiedNameSource.IMPORT),
        }

    For qualified name of a variable in a function or a comprehension, please refer
    :func:`~libcst.metadata.Scope.get_qualified_names_for` for more detail.
    """

    METADATA_DEPENDENCIES = (ScopeProvider,)

    def visit_Module(self, node: cst.Module) -> Optional[bool]:
        visitor = QualifiedNameVisitor(self)
        node.visit(visitor)

    @staticmethod
    def has_name(
        visitor: MetadataDependent, node: cst.CSTNode, name: Union[str, QualifiedName]
    ) -> bool:
        """Check if any of qualified name has the str name or :class:`~libcst.metadata.QualifiedName` name."""
        qualified_names = visitor.get_metadata(QualifiedNameProvider, node, set())
        if isinstance(name, str):
            return any(qn.name == name for qn in qualified_names)
        else:
            return any(qn == name for qn in qualified_names)


class QualifiedNameVisitor(cst.CSTVisitor):
    def __init__(self, provider: "QualifiedNameProvider") -> None:
        self.provider: QualifiedNameProvider = provider

    def on_visit(self, node: cst.CSTNode) -> bool:
        scope = self.provider.get_metadata(ScopeProvider, node, None)
        if scope:
            self.provider.set_metadata(
                node, LazyValue(lambda: scope.get_qualified_names_for(node))
            )
        else:
            self.provider.set_metadata(node, set())
        super().on_visit(node)
        return True


class FullyQualifiedNameProvider(BatchableMetadataProvider[Collection[QualifiedName]]):
    """
    Provide fully qualified names for CST nodes. Like :class:`QualifiedNameProvider`,
    but the provided :class:`QualifiedName` instances have absolute identifier names
    instead of local to the current module.

    This provider is initialized with the current module's fully qualified name, and can
    be used with :class:`~libcst.metadata.FullRepoManager`. The module's fully qualified
    name itself is stored as a metadata of the :class:`~libcst.Module` node. Compared to
    :class:`QualifiedNameProvider`, it also resolves relative imports.

    Example usage::

        >>> mgr = FullRepoManager(".", {"dir/a.py"}, {FullyQualifiedNameProvider})
        >>> wrapper = mgr.get_metadata_wrapper_for_path("dir/a.py")
        >>> fqnames = wrapper.resolve(FullyQualifiedNameProvider)
        >>> {type(k): v for (k, v) in fqnames.items()}
        {<class 'libcst._nodes.module.Module'>: {QualifiedName(name='dir.a', source=<QualifiedNameSource.LOCAL: 3>)}}

    """

    METADATA_DEPENDENCIES = (QualifiedNameProvider,)

    @classmethod
    def gen_cache(
        cls,
        root_path: Path,
        paths: List[str],
        *,
        use_pyproject_toml: bool = False,
        **kwargs: Any,
    ) -> Mapping[str, ModuleNameAndPackage]:
        cache = {
            path: calculate_module_and_package(
                root_path, path, use_pyproject_toml=use_pyproject_toml
            )
            for path in paths
        }
        return cache

    def __init__(self, cache: ModuleNameAndPackage) -> None:
        super().__init__(cache)
        self.module_name: str = cache.name
        self.package_name: str = cache.package

    def visit_Module(self, node: cst.Module) -> bool:
        visitor = FullyQualifiedNameVisitor(self, self.module_name, self.package_name)
        node.visit(visitor)
        self.set_metadata(
            node,
            {QualifiedName(name=self.module_name, source=QualifiedNameSource.LOCAL)},
        )
        return True


class FullyQualifiedNameVisitor(cst.CSTVisitor):
    @staticmethod
    def _fully_qualify_local(module_name: str, package_name: str, name: str) -> str:
        abs_name = name.lstrip(".")
        num_dots = len(name) - len(abs_name)
        # handle relative import
        if num_dots > 0:
            name = abs_name
            # see importlib._bootstrap._resolve_name
            # https://github.com/python/cpython/blob/3.10/Lib/importlib/_bootstrap.py#L902
            bits = package_name.rsplit(".", num_dots - 1)
            if len(bits) < num_dots:
                raise ImportError("attempted relative import beyond top-level package")
            module_name = bits[0]

        return f"{module_name}.{name}"

    @staticmethod
    def _fully_qualify(
        module_name: str, package_name: str, qname: QualifiedName
    ) -> QualifiedName:
        if qname.source == QualifiedNameSource.BUILTIN:
            # builtins are already fully qualified
            return qname
        name = qname.name
        if qname.source == QualifiedNameSource.IMPORT and not name.startswith("."):
            # non-relative imports are already fully qualified
            return qname
        new_name = FullyQualifiedNameVisitor._fully_qualify_local(
            module_name, package_name, qname.name
        )
        return dataclasses.replace(qname, name=new_name)

    def __init__(
        self, provider: FullyQualifiedNameProvider, module_name: str, package_name: str
    ) -> None:
        self.module_name = module_name
        self.package_name = package_name
        self.provider = provider

    def on_visit(self, node: cst.CSTNode) -> bool:
        qnames = self.provider.get_metadata(QualifiedNameProvider, node)
        if qnames is not None:
            self.provider.set_metadata(
                node,
                {
                    FullyQualifiedNameVisitor._fully_qualify(
                        self.module_name, self.package_name, qname
                    )
                    for qname in qnames
                },
            )
        return True
