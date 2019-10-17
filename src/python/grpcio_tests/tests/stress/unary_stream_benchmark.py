import datetime
import threading
import grpc
import subprocess
import sys
import time
import contextlib
import datetime
import sys

_PORT = 5741
_MESSAGE_SIZE = 4
_RESPONSE_COUNT = 32 * 1024


_SERVER_CODE = """
import datetime
import threading
import grpc
from concurrent import futures

def _handler_behavior(request, context):
  message_size, response_count = request.decode('ascii').split(',')
  for _ in range(int(response_count)):
    yield b'\\x00\\x01' * int(int(message_size) / 2)


class _Handler(grpc.GenericRpcHandler):
  def service(self, handler_call_details):
    return grpc.unary_stream_rpc_method_handler(_handler_behavior)


server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
server.add_insecure_port('[::]:%d')
server.add_generic_rpc_handlers((_Handler(),))
server.start()
server.wait_for_termination()
""" % _PORT

_GRPC_CHANNEL_OPTIONS = [
    ('grpc.max_metadata_size', 16 * 1024 * 1024),
    ('grpc.max_receive_message_length', 64 * 1024 * 1024)]


@contextlib.contextmanager
def _running_server():
  server_process = subprocess.Popen([sys.executable, '-c', _SERVER_CODE], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  try:
    yield
  finally:
    server_process.terminate()

def profile(message_size, response_count):
  with grpc.insecure_channel('[::]:{}'.format(_PORT), options=_GRPC_CHANNEL_OPTIONS) as channel:
    call = channel.unary_stream('foo')
    start = datetime.datetime.now()
    request = '{},{}'.format(message_size, response_count).encode('ascii')
    for message in call(request, wait_for_ready=True):
      pass
    end = datetime.datetime.now()
  return end - start

def main():
  with _running_server():
    for i in range(1000):
      latency = profile(_MESSAGE_SIZE, 1024)
      sys.stdout.write("{}\n".format(latency.total_seconds()))
      sys.stdout.flush()


if __name__ == '__main__':
  main()
