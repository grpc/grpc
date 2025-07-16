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
from collections import OrderedDict
from collections import abc
from typing import Any, Iterator, List, Optional, Tuple, Union

MetadataKey = Union[str, bytes]
MetadataValue = Union[str, bytes]

class Metadata(abc.Collection):  # noqa: PLW1641
    """Metadata abstraction for the asynchronous calls and interceptors.

    The metadata is a mapping from str -> List[str]

    Traits
        * Multiple entries are allowed for the same key
        * The order of the values by key is preserved
        * Getting by an element by key, retrieves the first mapped value
        * Supports an immutable view of the data
        * Allows partial mutation on the data without recreating the new object from scratch.
    """

    def __init__(self, *args: Tuple[MetadataKey, MetadataValue]) -> None:
        self._metadata = OrderedDict()
        for md_key, md_value in args:
            self.add(md_key, md_value)

    @classmethod
    def from_tuple(cls, raw_metadata: Optional[tuple]):
        if raw_metadata:
            return cls(*raw_metadata)
        return cls()

    def to_tuple(self) -> tuple:
        return tuple(self._metadata.items())

    def add(self, key: MetadataKey, value: MetadataValue) -> None:
        self._metadata.setdefault(key, [])
        self._metadata[key].append(value)

    def __len__(self) -> int:
        """Return the total number of elements that there are in the metadata,
        including multiple values for the same key.
        """
        return sum(map(len, self._metadata.values()))

    def __getitem__(self, key: MetadataKey) -> MetadataValue:
        """When calling <metadata>[<key>], the first element of all those
        mapped for <key> is returned.
        """
        try:
            return self._metadata[key][0]
        except (ValueError, IndexError) as e:
            error_msg = f"{key!r}"
            raise KeyError(error_msg) from e

    def __setitem__(self, key: MetadataKey, value: MetadataValue) -> None:
        """Calling metadata[<key>] = <value>
        Maps <value> to the first instance of <key>.
        """
        if key not in self:
            self._metadata[key] = [value]
        else:
            current_values = self.get_all(key)
            self._metadata[key] = [value, *current_values[1:]]

    def __delitem__(self, key: MetadataKey) -> None:
        """``del metadata[<key>]`` deletes the first mapping for <key>."""
        current_values = self.get_all(key)
        if not current_values:
            raise KeyError(repr(key))
        self._metadata[key] = current_values[1:]

    def delete_all(self, key: MetadataKey) -> None:
        """Delete all mappings for <key>."""
        del self._metadata[key]

    def __iter__(self) -> Iterator[Tuple[MetadataKey, MetadataValue]]:
        for key, values in self._metadata.items():
            for value in values:
                yield (key, value)

    def keys(self) -> abc.KeysView:
        return abc.KeysView(self)

    def values(self) -> abc.ValuesView:
        return abc.ValuesView(self)

    def items(self) -> abc.ItemsView:
        return abc.ItemsView(self)

    def get(
        self, key: MetadataKey, default: Optional[MetadataValue] = None
    ) -> Optional[MetadataValue]:
        try:
            return self[key]
        except KeyError:
            return default

    def get_all(self, key: MetadataKey) -> List[MetadataValue]:
        """For compatibility with other Metadata abstraction objects (like in Java),
        this would return all items under the desired <key>.
        """
        return self._metadata.get(key, [])

    def set_all(self, key: MetadataKey, values: List[MetadataValue]) -> None:
        self._metadata[key] = values

    def __contains__(self, key: MetadataKey) -> bool:
        return key in self._metadata

    def __eq__(self, other: object) -> bool:
        if isinstance(other, self.__class__):
            return self._metadata == other._metadata
        if isinstance(other, tuple):
            return tuple(self) == other
        return NotImplemented  # pytype: disable=bad-return-type

    def __add__(self, other: Any) -> "Metadata":
        if isinstance(other, self.__class__):
            return Metadata(*(tuple(self) + tuple(other)))
        if isinstance(other, tuple):
            return Metadata(*(tuple(self) + other))
        return NotImplemented  # pytype: disable=bad-return-type

    def __repr__(self) -> str:
        view = tuple(self)
        return "{0}({1!r})".format(self.__class__.__name__, view)


class MetadataValidator:
    @classmethod
    def is_metadatum_type(cls, item: Any) -> bool:
        """
        Checks if an item conforms to MetadatumType (Tuple[str, Union[str, bytes]]).
        """
        if not isinstance(item, (tuple, list)):
            return False
        if len(item) != 2:
            return False
        key, value = item
        if not isinstance(key, (str, bytes)):
            return False
        if not isinstance(value, (str, bytes)):
            return False
        return True

    @classmethod
    def is_valid_sequence_of_metadatum_type(cls, data: Any) -> bool:
        """
        Checks if the input is a sequence (tuple or list) where every element
        conforms to MetadatumType.
        """
        # Allow empty sequences as valid
        if not data:
            return True

        # 1. Check if the input itself is a sequence type you expect (e.g., tuple or list)
        if not isinstance(data, (tuple, list)):
            return False

        # 2. Iterate through each item and validate using is_metadatum_type
        return all(cls.is_metadatum_type(item) for item in data)

    @classmethod
    def validate_and_initialize(
        cls, metadata: Optional[Any] = None
    ) -> Metadata:
        metadata = metadata or Metadata()
        if not isinstance(metadata, Metadata) and isinstance(
            metadata, (tuple, list)
        ):
            if cls.is_valid_sequence_of_metadatum_type(metadata):
                return Metadata.from_tuple(tuple(metadata))
            else:
                raise TypeError(f"Invalid metadata format: {metadata!r}")
        return metadata
