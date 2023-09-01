# Copyright 2018 gRPC authors.
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


# No-op implementations for Windows.

def fork_handlers_and_grpc_init():
    grpc_init()


class ForkManagedThread(object):
    def __init__(self, target, args=()):
        self._thread = threading.Thread(target=_run_with_context(target), args=args)

    def setDaemon(self, daemonic):
        self._thread.daemon = daemonic

    def start(self):
        self._thread.start()

    def join(self):
        self._thread.join()


def block_if_fork_in_progress(postfork_state_to_reset=None):
    pass


def enter_user_request_generator():
    pass


def return_from_user_request_generator():
    pass


def get_fork_epoch():
    return 0


def is_fork_support_enabled():
    return False


def fork_register_channel(channel):
    pass


def fork_unregister_channel(channel):
    pass
