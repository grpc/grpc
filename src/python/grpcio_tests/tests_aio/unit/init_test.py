# Copyright 2019 The gRPC Authors.
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
import logging
import warnings
from typing_extensions import override
import unittest


class TestInit(unittest.TestCase):
    @classmethod
    @override
    def setUpClass(cls):
        cls.setup_logging()

    @staticmethod
    def setup_logging():
        # Logging configuration compatible with bazel-based runner.
        warnings.simplefilter("default")
        logging.basicConfig(
            level=logging.INFO,
            style="{",
            format="{levelname[0]}{asctime}.{msecs:03.0f} {thread} {threadName} {filename}:{lineno}] {message}",
            datefmt="%m%d %H:%M:%S",
        )

    def test_grpc(self):
        import grpc  # pylint: disable=wrong-import-position

        channel = grpc.aio.insecure_channel("phony")
        self.log_chan(channel, f"created grpc")
        self.assertIsInstance(channel, grpc.aio.Channel)

    def test_grpc_dot_aio(self):
        import grpc.aio  # pylint: disable=wrong-import-position

        channel = grpc.aio.insecure_channel("phony")
        self.log_chan(channel, f"created grpc.aio")
        self.assertIsInstance(channel, grpc.aio.Channel)

    @staticmethod
    def log_chan(chan, msg="") -> None:
        if msg:
            msg += " "

        cy_chan = chan._channel
        target = "unset"
        try:
            target = cy_chan._target.decode()
        except AttributeError:
            pass

        cy_ch_loop = ""
        try:
            cy_ch_loop = f" {id(cy_chan.loop)=}"
        except AttributeError:
            pass

        # f"[Thread {threading.current_thread().name}]"
        logging.info(f"{msg}Channel<{target=} {id(chan._loop)=}{cy_ch_loop}>")


if __name__ == "__main__":
    unittest.main(verbosity=2)
