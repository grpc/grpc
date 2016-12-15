import http2_base_server

class TestcaseRstStreamAfterHeader(object):
  """
    In response to an incoming request, this test sends headers, followed by
    a reset stream frame. Client asserts that the RPC failed.
  """
  def __init__(self):
    self._base_server = http2_base_server.H2ProtocolBaseServer()
    self._base_server._handlers['RequestReceived'] = self.on_request_received

  def get_base_server(self):
    return self._base_server

  def on_request_received(self, event):
    # send initial headers
    self._base_server.on_request_received_default(event)
    # send reset stream
    self._base_server.send_reset_stream()
