# Copyright 2025 The gRPC Authors
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

"""CLI entry point for gRPC Sleuth."""

import sys
from . import sleuth_lib

def main():
    """Main function for the sleuth CLI."""
    # sys.argv[0] is the script name, so pass the rest.
    exit_code = sleuth_lib.run_sleuth(sys.argv[1:])
    sys.exit(exit_code)

if __name__ == "__main__":
    main()

