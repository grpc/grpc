# Copyright 2015 gRPC authors.
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
"""Tools for creating tests of implementations of the Face layer."""

# unittest is referenced from specification in this module.
import unittest  # pylint: disable=unused-import

# test_interfaces is referenced from specification in this module.
from tests.unit.framework.interfaces.face import _blocking_invocation_inline_service
from tests.unit.framework.interfaces.face import _future_invocation_asynchronous_event_service
from tests.unit.framework.interfaces.face import _invocation
from tests.unit.framework.interfaces.face import test_interfaces  # pylint: disable=unused-import

_TEST_CASE_SUPERCLASSES = (
    _blocking_invocation_inline_service.TestCase,
    _future_invocation_asynchronous_event_service.TestCase,)


def test_cases(implementation):
    """Creates unittest.TestCase classes for a given Face layer implementation.

  Args:
    implementation: A test_interfaces.Implementation specifying creation and
      destruction of a given Face layer implementation.

  Returns:
    A sequence of subclasses of unittest.TestCase defining tests of the
      specified Face layer implementation.
  """
    test_case_classes = []
    for invoker_constructor in _invocation.invoker_constructors():
        for super_class in _TEST_CASE_SUPERCLASSES:
            test_case_classes.append(
                type(invoker_constructor.name() + super_class.NAME, (
                    super_class,), {
                        'implementation': implementation,
                        'invoker_constructor': invoker_constructor,
                        '__module__': implementation.__module__,
                    }))
    return test_case_classes
