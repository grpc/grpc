# Copyright 2023 gRPC authors.
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
from typing import Any


# TODO(sergiitk): All custom error classes should extend this.
class FrameworkError(Exception):
    """Base error class for framework errors."""

    message: str
    kwargs: dict[str, Any]
    note: str = ""

    def __init__(self, message: str, *args, **kwargs):
        self.message = message
        # Exception only stores args.
        self.kwargs = kwargs
        # Pass to the Exception as if message is in **args.
        super().__init__(*[message, *args])

    # TODO(sergiitk): Remove in py3.11, this will be built-in. See PEP 678.
    def add_note(self, note: str):
        self.note = note

    def __str__(self):
        return self.message if not self.note else f"{self.message}\n{self.note}"

    @classmethod
    def note_blanket_error(cls, reason: str) -> str:
        return (
            f"Important!!! This is a blanket error, just indicating {reason}."
            f" Further investigation must be done to determine the root cause."
        )

    @classmethod
    def note_blanket_error_info_below(
        cls, reason: str, *, info_below: str
    ) -> str:
        return (
            f"{cls.note_blanket_error(reason)}"
            f" Please inspect the information below:\n{info_below}"
        )
