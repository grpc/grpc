# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import argparse
import inspect
from abc import ABC, abstractmethod
from typing import Dict, Generator, List, Type, TypeVar

from libcst import Module
from libcst.codemod._codemod import Codemod
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareTransformer
from libcst.codemod.visitors._add_imports import AddImportsVisitor
from libcst.codemod.visitors._remove_imports import RemoveImportsVisitor

_Codemod = TypeVar("_Codemod", bound=Codemod)


class CodemodCommand(Codemod, ABC):
    """
    A :class:`~libcst.codemod.Codemod` which can be invoked on the command-line
    using the ``libcst.tool codemod`` utility. It behaves like any other codemod
    in that it can be instantiated and run identically to a
    :class:`~libcst.codemod.Codemod`. However, it provides support for providing
    help text and command-line arguments to ``libcst.tool codemod`` as well as
    facilities for automatically running certain common transforms after executing
    your :meth:`~libcst.codemod.Codemod.transform_module_impl`.

    The following list of transforms are automatically run at this time:

     - :class:`~libcst.codemod.visitors.AddImportsVisitor` (adds needed imports to a module).
     - :class:`~libcst.codemod.visitors.RemoveImportsVisitor` (removes unreferenced imports from a module).
    """

    #: An overrideable description attribute so that codemods can provide
    #: a short summary of what they do. This description will show up in
    #: command-line help as well as when listing available codemods.
    DESCRIPTION: str = "No description."

    @staticmethod
    def add_args(arg_parser: argparse.ArgumentParser) -> None:
        """
        Override this to add arguments to the CLI argument parser. These args
        will show up when the user invokes ``libcst.tool codemod`` with
        ``--help``. They will also be presented to your class's ``__init__``
        method. So, if you define a command with an argument 'foo', you should also
        have a corresponding 'foo' positional or keyword argument in your
        class's ``__init__`` method.
        """

        pass

    def _instantiate_and_run(self, transform: Type[_Codemod], tree: Module) -> Module:
        inst = transform(self.context)
        return inst.transform_module(tree)

    @abstractmethod
    def transform_module_impl(self, tree: Module) -> Module:
        """
        Override this with your transform. You should take in the tree, optionally
        mutate it and then return the mutated version. The module reference and all
        calculated metadata are available for the lifetime of this function.
        """
        ...

    def transform_module(self, tree: Module) -> Module:
        # Overrides (but then calls) Codemod's transform_module to provide
        # a spot where additional supported transforms can be attached and run.
        tree = super().transform_module(tree)

        # List of transforms we should run, with their context key they use
        # for storing in context.scratch. Typically, the transform will also
        # have a static method that other transforms can use which takes
        # a context and other optional args and modifies its own context key
        # accordingly. We import them here so that we don't have circular imports.
        supported_transforms: Dict[str, Type[Codemod]] = {
            AddImportsVisitor.CONTEXT_KEY: AddImportsVisitor,
            RemoveImportsVisitor.CONTEXT_KEY: RemoveImportsVisitor,
        }

        # For any visitors that we support auto-running, run them here if needed.
        for key, transform in supported_transforms.items():
            if key in self.context.scratch:
                # We have work to do, so lets run this.
                tree = self._instantiate_and_run(transform, tree)

        # We're finally done!
        return tree


class VisitorBasedCodemodCommand(ContextAwareTransformer, CodemodCommand, ABC):
    """
    A command that acts identically to a visitor-based transform, but also has
    the support of :meth:`~libcst.codemod.CodemodCommand.add_args` and running
    supported helper transforms after execution. See
    :class:`~libcst.codemod.CodemodCommand` and
    :class:`~libcst.codemod.ContextAwareTransformer` for additional documentation.
    """

    pass


class MagicArgsCodemodCommand(CodemodCommand, ABC):
    """
    A "magic" args command, which auto-magically looks up the transforms that
    are yielded from :meth:`~libcst.codemod.MagicArgsCodemodCommand.get_transforms`
    and instantiates them using values out of the context. Visitors yielded in
    :meth:`~libcst.codemod.MagicArgsCodemodCommand.get_transforms` must have
    constructor arguments that match a key in the context
    :attr:`~libcst.codemod.CodemodContext.scratch`. The easiest way to
    guarantee that is to use :meth:`~libcst.codemod.CodemodCommand.add_args`
    to add a command arg that will be parsed for each of the args. However, if
    you wish to chain transforms, adding to the scratch in one transform will make
    the value available to the constructor in subsequent transforms as well as the
    scratch for subsequent transforms.
    """

    def __init__(self, context: CodemodContext, **kwargs: Dict[str, object]) -> None:
        super().__init__(context)
        self.context.scratch.update(kwargs)

    @abstractmethod
    def get_transforms(self) -> Generator[Type[Codemod], None, None]:
        """
        A generator which yields one or more subclasses of
        :class:`~libcst.codemod.Codemod`. In the general case, you will usually
        yield a series of classes, but it is possible to programmatically decide
        which classes to yield depending on the contents of the context
        :attr:`~libcst.codemod.CodemodContext.scratch`.

        Note that you should yield classes, not instances of classes, as the
        point of :class:`~libcst.codemod.MagicArgsCodemodCommand` is to
        instantiate them for you with the contents of
        :attr:`~libcst.codemod.CodemodContext.scratch`.
        """
        ...

    def _instantiate(self, transform: Type[_Codemod]) -> _Codemod:
        # Grab the expected arguments
        argspec = inspect.getfullargspec(transform.__init__)
        args: List[object] = []
        kwargs: Dict[str, object] = {}
        last_default_arg = len(argspec.args) - len(argspec.defaults or ())
        for i, arg in enumerate(argspec.args):
            if arg in ["self", "context"]:
                # Self is bound, and context we explicitly include below.
                continue
            if arg not in self.context.scratch:
                if i >= last_default_arg:
                    # This arg has a default, so the fact that its missing is fine.
                    continue
                raise KeyError(
                    f"Visitor {transform.__name__} requires positional arg {arg} but "
                    + "it is not in our context nor does it have a default! It should "
                    + "be provided by an argument returned from the 'add_args' method "
                    + "or populated into context.scratch by a previous transform!"
                )
            # No default, but we found something in scratch. So, forward it.
            args.append(self.context.scratch[arg])
        kwonlydefaults = argspec.kwonlydefaults or {}
        for kwarg in argspec.kwonlyargs:
            if kwarg not in self.context.scratch and kwarg not in kwonlydefaults:
                raise KeyError(
                    f"Visitor {transform.__name__} requires keyword arg {kwarg} but "
                    + "it is not in our context nor does it have a default! It should "
                    + "be provided by an argument returned from the 'add_args' method "
                    + "or populated into context.scratch by a previous transform!"
                )
            kwargs[kwarg] = self.context.scratch.get(kwarg, kwonlydefaults[kwarg])

        # Return an instance of the transform with those arguments
        return transform(self.context, *args, **kwargs)

    def transform_module_impl(self, tree: Module) -> Module:
        for transform in self.get_transforms():
            inst = self._instantiate(transform)
            tree = inst.transform_module(tree)
        return tree
