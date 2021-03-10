#!/usr/bin/env python3
# Copyright 2017 gRPC authors.
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
"""
Wrapper around port server starting code.

Used by developers who wish to run individual C/C++ tests outside of the
run_tests.py infrastructure.

The path to this file is called out in test/core/util/port.c, and printed as
an error message to users.
"""

import python_utils.start_port_server as start_port_server

start_port_server.start_port_server()

print("Port server started successfully")
