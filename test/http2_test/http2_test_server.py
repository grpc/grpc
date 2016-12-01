"""
  HTTP2 Test Server. Highly experimental work in progress.
"""
import argparse
import logging

from twisted.internet.protocol import Factory
from twisted.internet import endpoints, reactor
import http2_base_server
import test_rst_after_header
import test_rst_after_data
import test_goaway
import test_ping
import test_max_streams

class H2Factory(Factory):
  def __init__(self, testcase):
    logging.info('In H2Factory')
    self._num_streams = 0
    self._testcase = testcase

  def buildProtocol(self, addr):
    self._num_streams += 1
    if self._testcase == 'rst_after_header':
      t = test_rst_after_header.TestcaseRstStreamAfterHeader()
    elif self._testcase == 'rst_after_data':
      t = test_rst_after_data.TestcaseRstStreamAfterData()
    elif self._testcase == 'goaway':
      t = test_goaway.TestcaseGoaway(self._num_streams)
    elif self._testcase == 'ping':
      t = test_ping.TestcasePing()
    elif self._testcase == 'max_streams':
      t = TestcaseSettingsMaxStreams(self._num_streams)
    else:
      logging.error('Unknown test case: %s'%self._testcase)
      assert(0)
    return t.get_base_server()

if __name__ == "__main__":
  logging.basicConfig(format = "%(levelname) -10s %(asctime)s %(module)s:%(lineno)s | %(message)s", level=logging.INFO)
  parser = argparse.ArgumentParser()
  parser.add_argument("test")
  parser.add_argument("port")
  args = parser.parse_args()
  if args.test not in ['rst_after_header', 'rst_after_data', 'goaway', 'ping', 'max_streams']:
    print 'unknown test: ', args.test
  endpoint = endpoints.TCP4ServerEndpoint(reactor, int(args.port), backlog=128)
  endpoint.listen(H2Factory(args.test))
  reactor.run()
