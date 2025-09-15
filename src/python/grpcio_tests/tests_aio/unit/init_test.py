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

import warnings, logging
import threading

warnings.simplefilter("default")
# logging.basicConfig(
#     level=logging.DEBUG,
#     style="{",
#     format="{levelname[0]}{asctime}.{msecs:03.0f} {thread} {filename}:{lineno}] {message}",
#     datefmt="%m%d %H:%M:%S",
# )
logging.basicConfig(
    level=logging.DEBUG,
    style="{",
    format="{levelname[0]}{asctime}.{msecs:03.0f} {thread} {threadName} {filename}:{lineno}] {message}",
    datefmt="%m%d %H:%M:%S",
)

import logging
import sys
import time
import unittest


class TestInit(unittest.TestCase):
    def test_grpc(self):
        import grpc  # pylint: disable=wrong-import-position

        channel = grpc.aio.insecure_channel("phony")
        self.log_chan(channel)
        self.assertIsInstance(channel, grpc.aio.Channel)
        channel2 = grpc.aio.insecure_channel("phony")
        self.log_chan(channel2)

    # def test_grpc_dot_aio(self):
    #     import grpc.aio  # pylint: disable=wrong-import-position

    #     channel = grpc.aio.insecure_channel("phony")
    #     self.log_chan(channel)
    #     self.assertIsInstance(channel, grpc.aio.Channel)

    def test_grpc_zzz_thread_pool(self):
        from concurrent.futures import ThreadPoolExecutor

        with ThreadPoolExecutor(max_workers=3) as executor:
            logging.info("Submitting tasks using executor.map()...")
            results_iterator = executor.map(self.make_chan, list(range(10)))

            logging.info("Collecting results...")
            for chan in results_iterator:
                # {chan._loop=},
                # {chan._channel.loop=}
                # logging.info(
                #     f"{chan.__class__.__module__}.{chan.__class__.__qualname__}"
                # )
                # {chan._channel._target=}
                # print(dir(chan._channel))
                self.log_chan(chan)

        # executor = ThreadPoolExecutor()

        # futures = []
        # for num in range(10):
        #     future = executor.submit(self.make_chan, num)
        #     futures.append(future)
        # time.sleep(10)

    @staticmethod
    def make_chan(num) -> None:
        # logging.basicConfig(
        #     level=logging.DEBUG,
        #     style="{",
        #     format="{levelname[0]}{asctime}.{msecs:03.0f} {thread} {filename}:{lineno}] {message}",
        #     datefmt="%m%d %H:%M:%S",
        # )

        # logging.info(
        #     f"[Thread {threading.current_thread().name}] make_chan({num=})"
        # )

        logging.info(f"make_chan({num=})")
        import grpc

        return grpc.aio.insecure_channel(f"localhost:5005{num}")
        # return grpc.insecure_channel("localhost:50051")

    @staticmethod
    def log_chan(chan) -> None:
        cy_chan = chan._channel
        target = cy_chan._target.decode()
        # f"[Thread {threading.current_thread().name}]"
        logging.info(
            f"Channel<{target=} {id(chan._loop)=} {id(cy_chan.loop)=}>"
        )


if __name__ == "__main__":
    # logging.basicConfig(level=logging.DEBUG)
    unittest.main()
    # unittest.main(verbosity=2)
