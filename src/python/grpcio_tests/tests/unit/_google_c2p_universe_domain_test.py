# Copyright 2026 gRPC authors.
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
"""Tests google-c2p universe domain configuration API."""

import logging
import unittest
import warnings

import grpc
import grpc.experimental


class GoogleC2PUniverseDomainTest(unittest.TestCase):

    def test_set_universe_domain_validation_and_idempotency(self):
        with warnings.catch_warnings():
            warnings.simplefilter(
                "ignore", grpc.experimental.ExperimentalApiWarning
            )
            with self.assertRaises(ValueError):
                grpc.experimental.google_c2p_set_universe_domain("")
            grpc.experimental.google_c2p_set_universe_domain("example.com")
            # Idempotent when set to the same value.
            grpc.experimental.google_c2p_set_universe_domain("example.com")
            # Conflicting value raises ValueError.
            with self.assertRaises(ValueError):
                grpc.experimental.google_c2p_set_universe_domain(
                    "other.example.com"
                )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
