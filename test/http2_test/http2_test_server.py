# Copyright 2016 gRPC authors.
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
"""HTTP2 Test Server"""

import argparse
import logging
import sys
import twisted
import twisted.internet
import twisted.internet.endpoints
import twisted.internet.reactor

import http2_base_server
import test_goaway
import test_max_streams
import test_ping
import test_rst_after_data
import test_rst_after_header
import test_rst_during_data
import test_data_frame_padding

_TEST_CASE_MAPPING = {
    'rst_after_header': test_rst_after_header.TestcaseRstStreamAfterHeader,
    'rst_after_data': test_rst_after_data.TestcaseRstStreamAfterData,
    'rst_during_data': test_rst_during_data.TestcaseRstStreamDuringData,
    'goaway': test_goaway.TestcaseGoaway,
    'ping': test_ping.TestcasePing,
    'max_streams': test_max_streams.TestcaseSettingsMaxStreams,

    # Positive tests below:
    'data_frame_padding': test_data_frame_padding.TestDataFramePadding,
    'no_df_padding_sanity_test': test_data_frame_padding.TestDataFramePadding,
}

_exit_code = 0


class H2Factory(twisted.internet.protocol.Factory):

    def __init__(self, testcase):
        logging.info('Creating H2Factory for new connection (%s)', testcase)
        self._num_streams = 0
        self._testcase = testcase

    def buildProtocol(self, addr):
        self._num_streams += 1
        logging.info('New Connection: %d' % self._num_streams)
        if not _TEST_CASE_MAPPING.has_key(self._testcase):
            logging.error('Unknown test case: %s' % self._testcase)
            assert (0)
        else:
            t = _TEST_CASE_MAPPING[self._testcase]

        if self._testcase == 'goaway':
            return t(self._num_streams).get_base_server()
        elif self._testcase == 'no_df_padding_sanity_test':
            return t(use_padding=False).get_base_server()
        else:
            return t().get_base_server()


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--base_port',
        type=int,
        default=8080,
        help='base port to run the servers (default: 8080). One test server is '
        'started on each incrementing port, beginning with base_port, in the '
        'following order: data_frame_padding,goaway,max_streams,'
        'no_df_padding_sanity_test,ping,rst_after_data,rst_after_header,'
        'rst_during_data')
    return parser.parse_args()


def listen(endpoint, test_case):
    deferred = endpoint.listen(H2Factory(test_case))

    def listen_error(reason):
        # If listening fails, we stop the reactor and exit the program
        # with exit code 1.
        global _exit_code
        _exit_code = 1
        logging.error('Listening failed: %s' % reason.value)
        twisted.internet.reactor.stop()

    deferred.addErrback(listen_error)


def start_test_servers(base_port):
    """ Start one server per test case on incrementing port numbers
  beginning with base_port """
    index = 0
    for test_case in sorted(_TEST_CASE_MAPPING.keys()):
        portnum = base_port + index
        logging.warning('serving on port %d : %s' % (portnum, test_case))
        endpoint = twisted.internet.endpoints.TCP4ServerEndpoint(
            twisted.internet.reactor, portnum, backlog=128)
        # Wait until the reactor is running before calling endpoint.listen().
        twisted.internet.reactor.callWhenRunning(listen, endpoint, test_case)

        index += 1


if __name__ == '__main__':
    logging.basicConfig(
        format=
        '%(levelname) -10s %(asctime)s %(module)s:%(lineno)s | %(message)s',
        level=logging.INFO)
    args = parse_arguments()
    start_test_servers(args.base_port)
    twisted.internet.reactor.run()
    sys.exit(_exit_code)
