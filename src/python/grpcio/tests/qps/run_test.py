#!/usr/bin/env python
import argparse
import threading
import time
import sys
import os

curr_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(1, curr_dir)
sys.path.insert(2, curr_dir + '/../')
sys.path.insert(3, curr_dir + '/../../')

import grpc.beta.implementations as implementations

import qps_worker
import src.proto.grpc.testing.services_pb2 as services_pb2
from src.proto.grpc.testing.control_pb2 import *
from src.proto.grpc.testing.payloads_pb2 import PayloadConfig, SimpleProtoParams
from src.proto.grpc.testing.stats_pb2 import HistogramParams

""" Utility script for running simple tests locally. This script creates
a qps_worker and client to launch the tests.  Run this script using
./run_test.py (parameters)
""" 

TEST_DURATION = 10
QPS_WORKER_PORT = 8080
TIMEOUT = 2*TEST_DURATION + 5

def client_config(args, server_port): 
  if args.alpha > 0:
    load_params = LoadParams(poisson=PoissonParams(offered_load=args.alpha))
  else:
    load_params = LoadParams(closed_loop=ClosedLoopParams())
  payload_config = PayloadConfig(simple_params=
      SimpleProtoParams(req_size=args.req_size, resp_size=args.resp_size))
  if args.client == 'unarysync':
    client_type = SYNC_CLIENT
    rpc_type = UNARY
  elif args.client == 'unaryasync':
    client_type = ASYNC_CLIENT
    rpc_type = UNARY
  elif args.client == 'streaming':
    client_type = ASYNC_CLIENT
    rpc_type = STREAMING
  else:
    raise Exception('Invalid test {}'.format(args.client))
  if args.use_ssl:
    config = ClientConfig(
        server_targets=['localhost:{}'.format(server_port)],
        client_type=client_type,
        security_params=SecurityParams(use_test_ca=True),
        outstanding_rpcs_per_channel=args.max_rpcs,
        client_channels=1,
        rpc_type=rpc_type,
        load_params=load_params,
        payload_config=payload_config,
        histogram_params=HistogramParams(resolution=10, max_possible=1e9*10))
  else:
    config = ClientConfig(
        server_targets=['localhost:{}'.format(server_port)],
        client_type=client_type,
        outstanding_rpcs_per_channel=args.max_rpcs,
        client_channels=1,
        rpc_type=rpc_type,
        load_params=load_params,
        payload_config=payload_config,
        histogram_params=HistogramParams(resolution=10, max_possible=1e9*10))
  return ClientArgs(setup=config)

   
def client_requests(args, server_port, is_stopped):
  yield client_config(args, server_port)
  is_stopped.wait(TEST_DURATION)
  yield ClientArgs(mark=Mark(reset=False))
    
def server_config(args):
  if args.use_ssl:
    config = ServerConfig(
        server_type=SYNC_SERVER,
        security_params = SecurityParams(use_test_ca=True),
        port=0)
  else:
    config = ServerConfig(
        server_type=SYNC_SERVER,
        port=0)
  return ServerArgs(setup=config)

def server_requests(args, is_stopped):
  yield server_config(args)
  is_stopped.wait()

def create_worker():
  worker_server = threading.Thread(target=qps_worker.run_worker_server, args=(QPS_WORKER_PORT,))
  worker_server.start()
  channel = implementations.insecure_channel('localhost', QPS_WORKER_PORT)
  stub = services_pb2.beta_create_WorkerService_stub(channel)
  return stub

def run_test(stub, args):
  try:
    server_stop = threading.Event()
    client_stop = threading.Event()
    server_port = next(stub.RunServer(server_requests(args, server_stop), TIMEOUT)).port
    
    result_iterator = stub.RunClient(client_requests(args, server_port, client_stop), TIMEOUT)
    next(result_iterator)
    stats = next(result_iterator).stats.latencies

    print("Min: {}".format(stats.min_seen))
    print("Max: {}".format(stats.max_seen))
    print("Avg: {}".format(stats.sum/max(stats.count, 1)))
    print("Buckets: {}".format(stats.bucket))

  except Exception as e:
    import traceback; traceback.print_exc()
  finally:
    client_stop.set()
    #Hacky, but used to drain outstanding requests
    time.sleep(1)
    server_stop.set()
    stub.QuitWorker(Void(), TIMEOUT)
    stub = None

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Run gRPC qps tests locally')
  parser.add_argument('client', choices=['unarysync', 'unaryasync', 'streaming'],
                   help='The type of client used in the test')
  parser.add_argument('--max_rpcs', type=int, default=1,
                   help='The max number of concurrent rpcs')
  parser.add_argument('--alpha', type=float, default=-1.0,
                   help='The alpha value for poisson requests (Open loop test)')
  parser.add_argument('--req_size', type=int, default=1000,
                   help='The size of request payload (bytes)')
  parser.add_argument('--resp_size', type=int, default=1000,
                   help='The size of the response payload (bytes)')
  parser.add_argument('--ssl', dest='use_ssl', action='store_true',
                   help='Enable ssl on the test channels')
  parser.set_defaults(use_ssl=False)
  args = parser.parse_args()
  run_test(create_worker(), args)
