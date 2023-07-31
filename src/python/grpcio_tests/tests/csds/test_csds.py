# Copyright 2021 The gRPC Authors
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
"""A simple test to ensure that the Python wrapper can get xDS config."""

from concurrent.futures import ThreadPoolExecutor
import logging
import os
import queue
import sys
import time
import unittest

from envoy.service.status.v3 import csds_pb2
from envoy.service.status.v3 import csds_pb2_grpc
from google.protobuf import json_format
import grpc
import grpc_csds

_DUMMY_XDS_ADDRESS = "xds:///foo.bar"
_DUMMY_BOOTSTRAP_FILE = """
{
  \"xds_servers\": [
    {
      \"server_uri\": \"fake:///xds_server\",
      \"channel_creds\": [
        {
          \"type\": \"fake\"
        }
      ],
      \"server_features\": [\"xds_v3\"]
    }
  ],
  \"node\": {
    \"id\": \"python_test_csds\",
    \"cluster\": \"test\",
    \"metadata\": {
      \"foo\": \"bar\"
    },
    \"locality\": {
      \"region\": \"corp\",
      \"zone\": \"svl\",
      \"sub_zone\": \"mp3\"
    }
  }
}\
"""


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class TestCsds(unittest.TestCase):
    def setUp(self):
        os.environ["GRPC_XDS_BOOTSTRAP_CONFIG"] = _DUMMY_BOOTSTRAP_FILE
        self._server = grpc.server(ThreadPoolExecutor())
        port = self._server.add_insecure_port("localhost:0")
        grpc_csds.add_csds_servicer(self._server)
        self._server.start()

        self._channel = grpc.insecure_channel(f"localhost:{port}")
        self._stub = csds_pb2_grpc.ClientStatusDiscoveryServiceStub(
            self._channel
        )

    def tearDown(self):
        self._channel.close()
        self._server.stop(0)
        os.environ.pop("GRPC_XDS_BOOTSTRAP_CONFIG", None)

    def get_xds_config_dump(self):
        return self._stub.FetchClientStatus(csds_pb2.ClientStatusRequest())

    def test_has_node(self):
        resp = self.get_xds_config_dump()
        self.assertEqual(1, len(resp.config))
        self.assertEqual("python_test_csds", resp.config[0].node.id)
        self.assertEqual("test", resp.config[0].node.cluster)

    def test_no_lds_found(self):
        dummy_channel = grpc.insecure_channel(_DUMMY_XDS_ADDRESS)

        # Force the XdsClient to initialize and request a resource
        with self.assertRaises(grpc.RpcError) as rpc_error:
            dummy_channel.unary_unary("")(b"", wait_for_ready=False, timeout=1)
        self.assertEqual(
            grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.exception.code()
        )

        # The resource request will fail with DOES_NOT_EXIST (after 15s)
        while True:
            resp = self.get_xds_config_dump()
            config = json_format.MessageToDict(resp)
            ok = False
            try:
                for xds_config in config["config"][0].get("xdsConfig", []):
                    if "listenerConfig" in xds_config:
                        listener = xds_config["listenerConfig"][
                            "dynamicListeners"
                        ][0]
                        if listener["clientStatus"] == "REQUESTED":
                            ok = True
                            break
                for generic_xds_config in config["config"][0].get(
                    "genericXdsConfigs", []
                ):
                    if "Listener" in generic_xds_config["typeUrl"]:
                        if generic_xds_config["clientStatus"] == "REQUESTED":
                            ok = True
                            break
            except KeyError as e:
                logging.debug("Invalid config: %s\n%s: %s", config, type(e), e)
                pass
            if ok:
                break
            time.sleep(1)
        dummy_channel.close()


@unittest.skipIf(
    sys.version_info[0] < 3, "ProtoBuf descriptor has moved on from Python2"
)
class TestCsdsStream(TestCsds):
    def get_xds_config_dump(self):
        if not hasattr(self, "request_queue"):
            request_queue = queue.Queue()
            response_iterator = self._stub.StreamClientStatus(
                iter(request_queue.get, None)
            )
        request_queue.put(csds_pb2.ClientStatusRequest())
        return next(response_iterator)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
