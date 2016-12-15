"""
  HTTP2 Test Server. Highly experimental work in progress.
"""
import argparse
import logging
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

_TEST_CASE_MAPPING = {
  'rst_after_header': test_rst_after_header.TestcaseRstStreamAfterHeader,
  'rst_after_data': test_rst_after_data.TestcaseRstStreamAfterData,
  'rst_during_data': test_rst_during_data.TestcaseRstStreamDuringData,
  'goaway': test_goaway.TestcaseGoaway,
  'ping': test_ping.TestcasePing,
  'max_streams': test_max_streams.TestcaseSettingsMaxStreams,
}

class H2Factory(twisted.internet.protocol.Factory):
  def __init__(self, testcase):
    logging.info('Creating H2Factory for new connection.')
    self._num_streams = 0
    self._testcase = testcase

  def buildProtocol(self, addr):
    self._num_streams += 1
    logging.info('New Connection: %d' % self._num_streams)
    if not _TEST_CASE_MAPPING.has_key(self._testcase):
      logging.error('Unknown test case: %s' % self._testcase)
      assert(0)
    else:
      t = _TEST_CASE_MAPPING[self._testcase]

    if self._testcase == 'goaway':
      return t(self._num_streams).get_base_server()
    else:
      return t().get_base_server()

if __name__ == "__main__":
  logging.basicConfig(format = "%(levelname) -10s %(asctime)s %(module)s:%(lineno)s | %(message)s", level=logging.INFO)
  parser = argparse.ArgumentParser()
  parser.add_argument("test")
  parser.add_argument("port")
  args = parser.parse_args()
  if args.test not in _TEST_CASE_MAPPING.keys():
    logging.error('unknown test: %s' % args.test)
  else:
    endpoint = twisted.internet.endpoints.TCP4ServerEndpoint(twisted.internet.reactor, int(args.port), backlog=128)
    endpoint.listen(H2Factory(args.test))
    twisted.internet.reactor.run()
