# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Implementation of the metadata abstraction for gRPC Asyncio Python."""
from typing import List, Tuple, AnyStr, Iterator, Any
from collections import abc, OrderedDict


class Metadata(abc.Mapping):
    """Metadata abstraction for the asynchronous calls and interceptors.

    The metadata is a mapping from str -> List[str]

    Traits
        * Multiple entries are allowed for the same key
        * The order of the values by key is preserved
        * Getting by an element by key, retrieves the first mapped value
        * Supports an immutable view of the data
    """

    def __init__(self, *args) -> None:
        self._metadata = OrderedDict()
        for md_key, md_value in args:
            self.add(md_key, md_value)

    def add(self, key: str, value: str) -> None:
        key = key.lower()
        self._metadata.setdefault(key, [])
        self._metadata[key].append(value)

    def __len__(self) -> int:
        return len(self._metadata)

    def __getitem__(self, key: str) -> str:
        try:
            first, *_ = self._metadata[key.lower()]
            return first
        except ValueError as e:
            raise KeyError("{0!r}".format(key)) from e

    def __iter__(self) -> Iterator[Tuple[AnyStr, AnyStr]]:
        for key, values in self._metadata.items():
            for value in values:
                yield (key, value)

    def view(self) -> Tuple[AnyStr, AnyStr]:
        return tuple(self)

    def get_all(self, key: str) -> List[str]:
        """For compatibility with other Metadata abstraction objects (like in Java),
        this would return all items under the desired <key>.
        """
        return self._metadata.get(key.lower(), [])

    def __contains__(self, key: str) -> bool:
        return key.lower() in self._metadata

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, self.__class__):
            return NotImplemented

        return self._metadata == other._metadata
