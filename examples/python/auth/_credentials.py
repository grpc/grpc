# Copyright 2019 The gRPC Authors
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
"""Loading SSL credentials for gRPC Python authentication example."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os


def _load_credential_from_file(filepath):
    real_path = os.path.join(os.path.dirname(__file__), filepath)
    with open(real_path, 'rb') as f:
        return f.read()


SERVER_CERTIFICATE = _load_credential_from_file('credentials/localhost.crt')
SERVER_CERTIFICATE_KEY = _load_credential_from_file('credentials/localhost.key')
ROOT_CERTIFICATE = _load_credential_from_file('credentials/root.crt')
