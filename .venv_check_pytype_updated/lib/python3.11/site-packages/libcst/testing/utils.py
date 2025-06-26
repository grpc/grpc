# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
# pyre-unsafe

import inspect
import re
from functools import wraps
from typing import (
    Any,
    Callable,
    Dict,
    Iterable,
    List,
    Mapping,
    Optional,
    Sequence,
    Tuple,
    TypeVar,
    Union,
)
from unittest import TestCase

DATA_PROVIDER_DATA_ATTR_NAME = "__data_provider_data"
DATA_PROVIDER_DESCRIPTION_PREFIX = "_data_provider_"
PROVIDER_TEST_LIMIT_ATTR_NAME = "__provider_test_limit"
DEFAULT_TEST_LIMIT = 256


T = TypeVar("T")


def none_throws(value: Optional[T], message: str = "Unexpected None value") -> T:
    assert value is not None, message
    return value


def update_test_limit(test_method: Any, test_limit: int) -> None:
    # Store the maximum number of generated tests on the test_method. Since
    # contextmanager_provider can be specified multiple times, we need to
    # take the maximum of the existing attribute and the current value
    existing_test_limit = getattr(
        test_method, PROVIDER_TEST_LIMIT_ATTR_NAME, test_limit
    )
    setattr(
        test_method, PROVIDER_TEST_LIMIT_ATTR_NAME, max(existing_test_limit, test_limit)
    )


def try_get_provider_attr(
    member_name: str, member: Any, attr_name: str
) -> Optional[Any]:
    if inspect.isfunction(member) and member_name.startswith("test"):
        return getattr(member, attr_name, None)
    return None


def populate_data_provider_tests(dct: Dict[str, Any]) -> None:
    test_methods_to_add: Dict[str, Callable] = {}
    test_methods_to_remove: List[str] = []
    for member_name, member in dct.items():
        provider_data = try_get_provider_attr(
            member_name, member, DATA_PROVIDER_DATA_ATTR_NAME
        )
        if provider_data is not None:
            for description, data in (
                provider_data.items()
                if isinstance(provider_data, dict)
                else enumerate(provider_data)
            ):
                if isinstance(provider_data, dict):
                    description = f"{DATA_PROVIDER_DESCRIPTION_PREFIX}{description}"

                assert re.fullmatch(
                    r"[a-zA-Z0-9_]+", str(description)
                ), f"Testcase description must be a valid python identifier: '{description}'"

                @wraps(member)
                def new_test(
                    self: object,
                    data: Iterable[object] = data,
                    member: Callable[..., object] = member,
                ) -> object:
                    if isinstance(data, dict):
                        return member(self, **data)
                    else:
                        return member(self, *data)

                name = f"{member_name}_{description}"
                new_test.__name__ = name
                test_methods_to_add[name] = new_test
            if not test_methods_to_add:
                raise ValueError(
                    f"No data_provider tests were created for {member_name}! Please double check your data."
                )
            test_methods_to_remove.append(member_name)
    dct.update(test_methods_to_add)

    # Remove all old methods
    for test_name in test_methods_to_remove:
        del dct[test_name]


def validate_provider_tests(dct: Dict[str, Any]) -> None:
    members_to_replace = {}

    for member_name, member in dct.items():
        test_limit = try_get_provider_attr(
            member_name, member, PROVIDER_TEST_LIMIT_ATTR_NAME
        )
        if test_limit is not None:
            data = try_get_provider_attr(
                member_name, member, DATA_PROVIDER_DATA_ATTR_NAME
            )
            num_tests = len(data) if data else 1

            if num_tests > test_limit:
                # We don't use wraps() here so that the test isn't expanded
                # as it normally would be by whichever provider it uses
                def test_replacement(
                    self: Any,
                    member_name: Any = member_name,
                    num_tests: Any = num_tests,
                    test_limit: Any = test_limit,
                ) -> None:
                    raise AssertionError(
                        f"{member_name} generated {num_tests} tests but the limit is "
                        + f"{test_limit}. You can increase the number of "
                        + "allowed tests by specifying test_limit, but please "
                        + "consider whether you really need to test all of "
                        + "these combinations."
                    )

                setattr(test_replacement, "__name__", member_name)
                members_to_replace[member_name] = test_replacement

    for member_name, new_member in members_to_replace.items():
        dct[member_name] = new_member


TestCaseType = Union[Sequence[object], Mapping[str, object]]
# Can't use Sequence[TestCaseType] here as some clients may pass in a Generator[TestCaseType]
StaticDataType = Union[Iterable[TestCaseType], Mapping[str, TestCaseType]]


def data_provider(
    static_data: StaticDataType, *, test_limit: int = DEFAULT_TEST_LIMIT
) -> Callable[[Callable], Callable]:
    # We need to be able to iterate over static_data more than once
    # (for validation), so if we weren't passed in a dict, list, or tuple
    # then we'll just create a list from the data
    if not isinstance(static_data, (dict, list, tuple)):
        static_data = list(static_data)

    def test_decorator(test_method: Callable) -> Callable:
        update_test_limit(test_method, test_limit)

        setattr(test_method, DATA_PROVIDER_DATA_ATTR_NAME, static_data)
        return test_method

    return test_decorator


class BaseTestMeta(type):
    def __new__(mcs, name: str, bases: Tuple[type, ...], dct: Dict[str, Any]) -> object:
        validate_provider_tests(dct)
        populate_data_provider_tests(dct)
        return super().__new__(mcs, name, bases, dict(dct))


class UnitTest(TestCase, metaclass=BaseTestMeta):
    pass
