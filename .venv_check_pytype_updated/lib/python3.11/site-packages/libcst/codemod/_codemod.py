# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from abc import ABC, abstractmethod
from contextlib import contextmanager
from dataclasses import replace
from typing import Generator

from libcst import MetadataDependent, MetadataWrapper, Module
from libcst.codemod._context import CodemodContext


class Codemod(MetadataDependent, ABC):
    """
    Abstract base class that all codemods must subclass from. Classes wishing
    to perform arbitrary, non-visitor-based mutations on a tree should subclass
    from this class directly. Classes wishing to perform visitor-based mutation
    should instead subclass from :class:`~libcst.codemod.ContextAwareTransformer`.

    Note that a :class:`~libcst.codemod.Codemod` is a subclass of
    :class:`~libcst.MetadataDependent`, meaning that you can declare metadata
    dependencies with the :attr:`~libcst.MetadataDependent.METADATA_DEPENDENCIES`
    class property and while you are executing a transform you can call
    :meth:`~libcst.MetadataDependent.get_metadata` to retrieve
    the resolved metadata.
    """

    def __init__(self, context: CodemodContext) -> None:
        MetadataDependent.__init__(self)
        self.context: CodemodContext = context

    def should_allow_multiple_passes(self) -> bool:
        """
        Override this and return ``True`` to allow your transform to be called
        repeatedly until the tree doesn't change between passes. By default,
        this is off, and should suffice for most transforms.
        """
        return False

    def warn(self, warning: str) -> None:
        """
        Emit a warning that is displayed to the user who has invoked this codemod.
        """
        self.context.warnings.append(warning)

    @property
    def module(self) -> Module:
        """
        Reference to the currently-traversed module. Note that this is only available
        during the execution of a codemod. The module reference is particularly
        handy if you want to use :meth:`libcst.Module.code_for_node` or
        :attr:`libcst.Module.config_for_parsing` and don't wish to track a reference
        to the top-level module manually.
        """
        module = self.context.module
        if module is None:
            raise ValueError(
                f"Attempted access of {self.__class__.__name__}.module outside of "
                "transform_module()."
            )
        return module

    @abstractmethod
    def transform_module_impl(self, tree: Module) -> Module:
        """
        Override this with your transform. You should take in the tree, optionally
        mutate it and then return the mutated version. The module reference and all
        calculated metadata are available for the lifetime of this function.
        """
        ...

    @contextmanager
    def _handle_metadata_reference(
        self, module: Module
    ) -> Generator[Module, None, None]:
        oldwrapper = self.context.wrapper
        metadata_manager = self.context.metadata_manager
        filename = self.context.filename
        if metadata_manager is not None and filename:
            # We can look up full-repo metadata for this codemod!
            cache = metadata_manager.get_cache_for_path(filename)
            wrapper = MetadataWrapper(module, cache=cache)
        else:
            # We are missing either the repo manager or the current path,
            # which can happen when we are codemodding from stdin or when
            # an upstream dependency manually instantiates us.
            wrapper = MetadataWrapper(module)

        with self.resolve(wrapper):
            self.context = replace(self.context, wrapper=wrapper)
            try:
                yield wrapper.module
            finally:
                self.context = replace(self.context, wrapper=oldwrapper)

    def transform_module(self, tree: Module) -> Module:
        """
        Transform entrypoint which handles multi-pass logic and metadata calculation
        for you. This is the method that you should call if you wish to invoke a
        codemod directly. This is the method that is called by
        :func:`~libcst.codemod.transform_module`.
        """

        if not self.should_allow_multiple_passes():
            with self._handle_metadata_reference(tree) as tree_with_metadata:
                return self.transform_module_impl(tree_with_metadata)

        # We allow multiple passes, so we execute 1+ passes until there are
        # no more changes.
        previous: Module = tree
        while True:
            with self._handle_metadata_reference(tree) as tree_with_metadata:
                tree = self.transform_module_impl(tree_with_metadata)
            if tree.deep_equals(previous):
                break
            previous = tree
        return tree
