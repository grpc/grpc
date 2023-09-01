# Copyright 2021 gRPC authors.
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
"""This contains common helpers for generating randomized data."""
import random
import string

import framework.helpers.datetime

# Alphanumeric characters, similar to regex [:alnum:] class, [a-zA-Z0-9]
ALPHANUM = string.ascii_letters + string.digits
# Lowercase alphanumeric characters: [a-z0-9]
# Use ALPHANUM_LOWERCASE alphabet when case-sensitivity is a concern.
ALPHANUM_LOWERCASE = string.ascii_lowercase + string.digits


def rand_string(length: int = 8, *, lowercase: bool = False) -> str:
    """Return random alphanumeric string of given length.

    Space for default arguments: alphabet^length
       lowercase and uppercase = (26*2 + 10)^8 = 2.18e14 = 218 trillion.
       lowercase only = (26 + 10)^8 = 2.8e12 = 2.8 trillion.
    """
    alphabet = ALPHANUM_LOWERCASE if lowercase else ALPHANUM
    return "".join(random.choices(population=alphabet, k=length))


def random_resource_suffix() -> str:
    """Return a ready-to-use resource suffix with datetime and nonce."""
    # Date and time suffix for debugging. Seconds skipped, not as relevant
    # Format example: 20210626-1859
    datetime_suffix: str = framework.helpers.datetime.datetime_suffix()
    # Use lowercase chars because some resource names won't allow uppercase.
    # For len 5, total (26 + 10)^5 = 60,466,176 combinations.
    # Approx. number of test runs needed to start at the same minute to
    # produce a collision: math.sqrt(math.pi/2 * (26+10)**5) ≈ 9745.
    # https://en.wikipedia.org/wiki/Birthday_attack#Mathematics
    unique_hash: str = rand_string(5, lowercase=True)
    return f"{datetime_suffix}-{unique_hash}"
