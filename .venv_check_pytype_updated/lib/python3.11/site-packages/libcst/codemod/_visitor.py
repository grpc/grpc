# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Mapping

import libcst as cst
from libcst import MetadataDependent, MetadataException
from libcst.codemod._codemod import Codemod
from libcst.codemod._context import CodemodContext
from libcst.matchers import MatcherDecoratableTransformer, MatcherDecoratableVisitor
from libcst.metadata import ProviderT


class ContextAwareTransformer(Codemod, MatcherDecoratableTransformer):
    """
    A transformer which visits using LibCST. Allows visitor-based mutation of a tree.
    Classes wishing to do arbitrary non-visitor-based mutation on a tree should
    instead subclass from :class:`Codemod` and implement
    :meth:`~Codemod.transform_module_impl`. This is a subclass of
    :class:`~libcst.matchers.MatcherDecoratableTransformer` so all features of matchers
    as well as :class:`~libcst.CSTTransformer` are available to subclasses of this
    class.
    """

    def __init__(self, context: CodemodContext) -> None:
        Codemod.__init__(self, context)
        MatcherDecoratableTransformer.__init__(self)

    def transform_module_impl(self, tree: cst.Module) -> cst.Module:
        return tree.visit(self)


class ContextAwareVisitor(MatcherDecoratableVisitor, MetadataDependent):
    """
    A visitor which visits using LibCST. Allows visitor-based collecting of info
    on a tree. All codemods which wish to implement an information collector should
    subclass from this instead of directly from
    :class:`~libcst.matchers.MatcherDecoratableVisitor` or :class:`~libcst.CSTVisitor`
    since this provides access to the current codemod context. As a result, this
    class allows access to metadata which was calculated in a parent
    :class:`~libcst.codemod.Codemod` through the
    :meth:`~libcst.MetadataDependent.get_metadata` method.

    Note that you cannot directly run a :class:`~libcst.codemod.ContextAwareVisitor`
    using :func:`~libcst.codemod.transform_module` because visitors by definition
    do not transform trees. However, you can instantiate a
    :class:`~libcst.codemod.ContextAwareVisitor` inside a codemod and pass it to the
    :class:`~libcst.CSTNode.visit` method on any node in order to run information
    gathering with metadata and context support.

    Remember that a :class:`~libcst.codemod.ContextAwareVisitor` is a subclass of
    :class:`~libcst.MetadataDependent`, meaning that you still need to declare
    your metadata dependencies with
    :attr:`~libcst.MetadataDependent.METADATA_DEPENDENCIES` before you can retrieve
    metadata using :meth:`~libcst.MetadataDependent.get_metadata`, even if the parent
    codemod has listed its own metadata dependencies. Note also that the dependencies
    listed on this class must be a strict subset of the dependencies listed in the
    parent codemod.
    """

    def __init__(self, context: CodemodContext) -> None:
        MetadataDependent.__init__(self)
        MatcherDecoratableVisitor.__init__(self)
        self.context = context

        dependencies = self.get_inherited_dependencies()
        if dependencies:
            wrapper = self.context.wrapper
            if wrapper is None:
                raise MetadataException(
                    f"Attempting to instantiate {self.__class__.__name__} outside of "
                    + "an active transform. This means that metadata hasn't been "
                    + "calculated and we cannot successfully create this visitor."
                )
            for dep in dependencies:
                if dep not in wrapper._metadata:
                    raise MetadataException(
                        f"Attempting to access metadata {dep.__name__} that was not a "
                        + "declared dependency of parent transform! This means it is "
                        + "not possible to compute this value. Please ensure that all "
                        + f"parent transforms of {self.__class__.__name__} declare "
                        + f"{dep.__name__} as a metadata dependency."
                    )
            self.metadata: Mapping[ProviderT, Mapping[cst.CSTNode, object]] = {
                dep: wrapper._metadata[dep] for dep in dependencies
            }

    def warn(self, warning: str) -> None:
        """
        Emit a warning that is displayed to the user who has invoked this codemod.
        """
        self.context.warnings.append(warning)

    @property
    def module(self) -> cst.Module:
        """
        Reference to the currently-traversed module. Note that this is only available
        during a transform itself.
        """
        module = self.context.module
        if module is None:
            raise ValueError(
                f"Attempted access of {self.__class__.__name__}.module outside of "
                + "transform_module()."
            )
        return module
