# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Entry points into the packet-exchange-based implementation the base layer."""

# interfaces is referenced from specification in this module.
from _framework.base.packets import _ends
from _framework.base.packets import interfaces  # pylint: disable=unused-import


def front(work_pool, transmission_pool, utility_pool):
  """Factory function for creating interfaces.Fronts.

  Args:
    work_pool: A thread pool to be used for doing work within the created Front
      object.
    transmission_pool: A thread pool to be used within the created Front object
      for transmitting values to some Back object.
    utility_pool: A thread pool to be used within the created Front object for
      utility tasks.

  Returns:
    An interfaces.Front.
  """
  return _ends.Front(work_pool, transmission_pool, utility_pool)


def back(
    servicer, work_pool, transmission_pool, utility_pool, default_timeout,
    maximum_timeout):
  """Factory function for creating interfaces.Backs.

  Args:
    servicer: An interfaces.Servicer for servicing operations.
    work_pool: A thread pool to be used for doing work within the created Back
      object.
    transmission_pool: A thread pool to be used within the created Back object
      for transmitting values to some Front object.
    utility_pool: A thread pool to be used within the created Back object for
      utility tasks.
    default_timeout: A length of time in seconds to be used as the default
      time alloted for a single operation.
    maximum_timeout: A length of time in seconds to be used as the maximum
      time alloted for a single operation.

  Returns:
    An interfaces.Back.
  """
  return _ends.Back(
      servicer, work_pool, transmission_pool, utility_pool, default_timeout,
      maximum_timeout)
