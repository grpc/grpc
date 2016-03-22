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

"""Entry points into the ticket-exchange-based base layer implementation."""

# interfaces is referenced from specification in this module.
from grpc.framework.base import _ends
from grpc.framework.base import interfaces  # pylint: disable=unused-import


def front_link(work_pool, transmission_pool, utility_pool):
  """Factory function for creating interfaces.FrontLinks.

  Args:
    work_pool: A thread pool to be used for doing work within the created
      FrontLink object.
    transmission_pool: A thread pool to be used within the created FrontLink
      object for transmitting values to a joined RearLink object.
    utility_pool: A thread pool to be used within the created FrontLink object
      for utility tasks.

  Returns:
    An interfaces.FrontLink.
  """
  return _ends.FrontLink(work_pool, transmission_pool, utility_pool)


def back_link(
    servicer, work_pool, transmission_pool, utility_pool, default_timeout,
    maximum_timeout):
  """Factory function for creating interfaces.BackLinks.

  Args:
    servicer: An interfaces.Servicer for servicing operations.
    work_pool: A thread pool to be used for doing work within the created
      BackLink object.
    transmission_pool: A thread pool to be used within the created BackLink
      object for transmitting values to a joined ForeLink object.
    utility_pool: A thread pool to be used within the created BackLink object
      for utility tasks.
    default_timeout: A length of time in seconds to be used as the default
      time alloted for a single operation.
    maximum_timeout: A length of time in seconds to be used as the maximum
      time alloted for a single operation.

  Returns:
    An interfaces.BackLink.
  """
  return _ends.BackLink(
      servicer, work_pool, transmission_pool, utility_pool, default_timeout,
      maximum_timeout)
