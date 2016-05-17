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

"""Private constants for the package."""

from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.links import links

TICKET_SUBSCRIPTION_FOR_BASE_SUBSCRIPTION_KIND = {
    base.Subscription.Kind.NONE: links.Ticket.Subscription.NONE,
    base.Subscription.Kind.TERMINATION_ONLY:
        links.Ticket.Subscription.TERMINATION,
    base.Subscription.Kind.FULL: links.Ticket.Subscription.FULL,
    }

# Mapping from abortive operation outcome to ticket termination to be
# sent to the other side of the operation, or None to indicate that no
# ticket should be sent to the other side in the event of such an
# outcome.
ABORTION_OUTCOME_TO_TICKET_TERMINATION = {
    base.Outcome.Kind.CANCELLED: links.Ticket.Termination.CANCELLATION,
    base.Outcome.Kind.EXPIRED: links.Ticket.Termination.EXPIRATION,
    base.Outcome.Kind.LOCAL_SHUTDOWN: links.Ticket.Termination.SHUTDOWN,
    base.Outcome.Kind.REMOTE_SHUTDOWN: None,
    base.Outcome.Kind.RECEPTION_FAILURE:
        links.Ticket.Termination.RECEPTION_FAILURE,
    base.Outcome.Kind.TRANSMISSION_FAILURE: None,
    base.Outcome.Kind.LOCAL_FAILURE: links.Ticket.Termination.LOCAL_FAILURE,
    base.Outcome.Kind.REMOTE_FAILURE: links.Ticket.Termination.REMOTE_FAILURE,
}

INTERNAL_ERROR_LOG_MESSAGE = ':-( RPC Framework (Core) internal error! )-:'
TERMINATION_CALLBACK_EXCEPTION_LOG_MESSAGE = (
    'Exception calling termination callback!')
