# Copyright 2025 The gRPC Authors.
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

import time
from typing_extensions import override
import warnings
import logging
import unittest


class TestWorkingLoop(unittest.TestCase):
    @classmethod
    @override
    def setUpClass(cls):
        # Logging configuration compatible with bazel-based runner.
        warnings.simplefilter("default")
        logging.basicConfig(
            level=logging.INFO,
            style="{",
            format="{levelname[0]}{asctime}.{msecs:03.0f} {thread} {threadName} {filename}:{lineno}] {message}",
            datefmt="%m%d %H:%M:%S",
        )

    @override
    def setUp(self):
        logging.info(f"=== starting test: {self.id()} ===")

    def test_grpc_thread_pool(self):
        from concurrent.futures import ThreadPoolExecutor
        from concurrent.futures import Future

        # with ThreadPoolExecutor(max_workers=3, thread_name_prefix="TPEC") as executor:
        #     logging.info("Submitting tasks using executor.map()...")
        #     results_iterator = executor.map(self.make_chan, list(range(10)))

        #     logging.info("Collecting results...")
        #     for chan in results_iterator:
        #         # {chan._loop=},
        #         # {chan._channel.loop=}
        #         # logging.info(
        #         #     f"{chan.__class__.__module__}.{chan.__class__.__qualname__}"
        #         # )
        #         # {chan._channel._target=}
        #         # print(dir(chan._channel))
        #         self.log_chan(chan)

        executor = ThreadPoolExecutor(max_workers=3, thread_name_prefix="TPE")

        futures = []
        for num in range(10):
            future: Future = executor.submit(self.make_chan, num)
            futures.append(future)

        time.sleep(1)

        logging.info("Collecting results...")
        for future in futures:
            ex = future.exception(timeout=3)
            if ex:
                logging.error(ex)
                continue
            chan = future.result()
            self.log_chan(chan, "back to main thread")

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

        logging.info(f"called make_chan({num=})")
        import grpc

        chan = grpc.aio.insecure_channel(f"localhost:5005{num}")
        TestWorkingLoop.log_chan(chan, f"make_chan({num=}) created chan ")
        return chan

    @staticmethod
    def log_chan(chan, msg="") -> None:
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
