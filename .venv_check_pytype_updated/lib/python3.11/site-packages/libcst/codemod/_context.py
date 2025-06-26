# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

import libcst as cst
import libcst.metadata as meta


@dataclass(frozen=True)
class CodemodContext:
    """
    A context holding all information that is shared amongst all transforms
    and visitors in a single codemod invocation. When chaining multiple
    transforms together, the context holds the state that needs to be passed
    between transforms. The context is responsible for keeping track of
    metadata wrappers and the filename of the file that is being modified
    (if available).
    """

    #: List of warnings gathered while running a codemod. Add to this list
    #: by calling :meth:`~libcst.codemod.Codemod.warn` method from a class
    #: that subclasses from :class:`~libcst.codemod.Codemod`,
    #: :class:`~libcst.codemod.ContextAwareTransformer` or
    #: :class:`~libcst.codemod.ContextAwareVisitor`.
    warnings: List[str] = field(default_factory=list)

    #: Scratch dictionary available for codemods which are spread across multiple
    #: transforms. Codemods are free to add to this at will.
    scratch: Dict[str, Any] = field(default_factory=dict)

    #: The current filename if a codemod is being executed against a file that
    #: lives on disk. Populated by
    #: :func:`libcst.codemod.parallel_exec_transform_with_prettyprint` when
    #: running codemods from the command line.
    filename: Optional[str] = None

    #: The current module if a codemod is being executed against a file that
    #: lives on disk, and the repository root is correctly configured. This
    #: Will take the form of a dotted name such as ``foo.bar.baz`` for a file
    #: in the repo named ``foo/bar/baz.py``.
    full_module_name: Optional[str] = None

    #: The current package if a codemod is being executed against a file that
    #: lives on disk, and the repository root is correctly configured. This
    #: Will take the form of a dotted name such as ``foo.bar`` for a file
    #: in the repo named ``foo/bar/baz.py``
    full_package_name: Optional[str] = None

    #: The current top level metadata wrapper for the module being modified.
    #: To access computed metadata when inside an actively running codemod, use
    #: the :meth:`~libcst.MetadataDependent.get_metadata` method on
    #: :class:`~libcst.codemod.Codemod`.
    wrapper: Optional[cst.MetadataWrapper] = None

    #: The current repo-level metadata manager for the active codemod.
    metadata_manager: Optional[meta.FullRepoManager] = None

    @property
    def module(self) -> Optional[cst.Module]:
        """
        The current top level module being modified. As a convenience, you can
        use the :attr:`~libcst.codemod.Codemod.module` property on
        :class:`~libcst.codemod.Codemod` to refer to this when inside an actively
        running codemod.
        """

        wrapper = self.wrapper
        if wrapper is None:
            return None
        return wrapper.module
