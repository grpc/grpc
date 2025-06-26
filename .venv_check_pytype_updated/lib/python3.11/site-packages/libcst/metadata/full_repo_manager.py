# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from pathlib import Path
from typing import Collection, Dict, List, Mapping, TYPE_CHECKING

import libcst as cst
from libcst._types import StrPath
from libcst.metadata.wrapper import MetadataWrapper

if TYPE_CHECKING:
    from libcst.metadata.base_provider import ProviderT  # noqa: F401


class FullRepoManager:
    def __init__(
        self,
        repo_root_dir: StrPath,
        paths: Collection[str],
        providers: Collection["ProviderT"],
        timeout: int = 5,
        use_pyproject_toml: bool = False,
    ) -> None:
        """
        Given project root directory with pyre and watchman setup, :class:`~libcst.metadata.FullRepoManager`
        handles the inter process communication to read the required full repository cache data for
        metadata provider like :class:`~libcst.metadata.TypeInferenceProvider`.

        :param paths: a collection of paths to access full repository data.
        :param providers: a collection of metadata provider classes require accessing full repository data, currently supports
            :class:`~libcst.metadata.TypeInferenceProvider` and
            :class:`~libcst.metadata.FullyQualifiedNameProvider`.
        :param timeout: number of seconds. Raises `TimeoutExpired <https://docs.python.org/3/library/subprocess.html#subprocess.TimeoutExpired>`_
            when timeout.
        """
        self.root_path: Path = Path(repo_root_dir)
        self._cache: Dict["ProviderT", Mapping[str, object]] = {}
        self._timeout = timeout
        self._use_pyproject_toml = use_pyproject_toml
        self._providers = providers
        self._paths: List[str] = list(paths)

    @property
    def cache(self) -> Dict["ProviderT", Mapping[str, object]]:
        """
        The full repository cache data for all metadata providers passed in the ``providers`` parameter when
        constructing :class:`~libcst.metadata.FullRepoManager`. Each provider is mapped to a mapping of path to cache.
        """
        # Make sure that the cache is available to us. If resolve_cache() was called manually then this is a noop.
        self.resolve_cache()
        return self._cache

    def resolve_cache(self) -> None:
        """
        Resolve cache for all providers that require it. Normally this is called by
        :meth:`~FullRepoManager.get_cache_for_path` so you do not need to call it
        manually. However, if you intend to do a single cache resolution pass before
        forking, it is a good idea to call this explicitly to control when cache
        resolution happens.
        """
        if not self._cache:
            cache: Dict["ProviderT", Mapping[str, object]] = {}
            for provider in self._providers:
                handler = provider.gen_cache
                if handler:
                    cache[provider] = handler(
                        self.root_path,
                        self._paths,
                        timeout=self._timeout,
                        use_pyproject_toml=self._use_pyproject_toml,
                    )
            self._cache = cache

    def get_cache_for_path(self, path: str) -> Mapping["ProviderT", object]:
        """
        Retrieve cache for a source file. The file needs to appear in the ``paths`` parameter when
        constructing :class:`~libcst.metadata.FullRepoManager`.

        .. code-block:: python

            manager = FullRepoManager(".", {"a.py", "b.py"}, {TypeInferenceProvider})
            MetadataWrapper(module, cache=manager.get_cache_for_path("a.py"))
        """
        if path not in self._paths:
            raise ValueError(
                "The path needs to be in paths parameter when constructing FullRepoManager for efficient batch processing."
            )
        # Make sure that the cache is available to us. If the user called
        # resolve_cache() manually then this is a noop.
        self.resolve_cache()
        return {
            provider: data
            for provider, files in self._cache.items()
            for _path, data in files.items()
            if _path == path
        }

    def get_metadata_wrapper_for_path(self, path: str) -> MetadataWrapper:
        """
        Create a :class:`~libcst.metadata.MetadataWrapper` given a source file path.
        The path needs to be a path relative to project root directory.
        The source code is read and parsed as :class:`~libcst.Module` for
        :class:`~libcst.metadata.MetadataWrapper`.

        .. code-block:: python

            manager = FullRepoManager(".", {"a.py", "b.py"}, {TypeInferenceProvider})
            wrapper = manager.get_metadata_wrapper_for_path("a.py")
        """
        module = cst.parse_module((self.root_path / path).read_text())
        cache = self.get_cache_for_path(path)
        return MetadataWrapper(module, True, cache)
