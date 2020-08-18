<?php
/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * This is the PHP xDS Interop test client. This script is meant to be run by
 * the main xDS Interep test runner "run_xds_tests.py", not to be run
 * by itself standalone.
 */
$autoload_path = realpath(dirname(__FILE__).'/../../vendor/autoload.php');
require_once $autoload_path;

// The main xds interop test runner will ping this service to ask for
// the stats of the distribution of the backends, for the next X rpcs.
class LoadBalancerStatsService
    extends \Grpc\Testing\LoadBalancerStatsServiceStub
{
    function getClientStats(\Grpc\Testing\LoadBalancerStatsRequest $request) {
        $num_rpcs = $request->getNumRpcs();
        $timeout_sec = $request->getTimeoutSec();
        $rpcs_by_method = [];
        $rpcs_by_peer = [];
        $num_failures = 0;

        // Heavy limitation now: the server is blocking, until all
        // the necessary num_rpcs are finished, or timeout is reached
        global $client_thread;
        $start_id = $client_thread->num_results + 1;
        $end_id = $start_id + $num_rpcs;
        $now = hrtime(true);
        $timeout = $now[0] + ($now[1] / 1e9) + $timeout_sec;
        while (true) {
            $curr_hr = hrtime(true);
            $curr_time = $curr_hr[0] + ($curr_hr[1] / 1e9);
            if ($curr_time > $timeout) {
                break;
            }
            // Thread variable seems to be read-only
            $curr_id = $client_thread->num_results;
            if ($curr_id >= $end_id) {
                break;
            }
            usleep(50000);
        }

        // Tally up results
        $end_id = min($end_id, $client_thread->num_results);
        // "$client_thread->results" will be in the form of
        // [
        //   'rpc1' => [
        //     'hostname1', '', 'hostname2', 'hostname1', '', ...
        //   ],
        //   'rpc2' => [
        //     '', 'hostname1', 'hostname2', '', 'hostname2', ...
        //   ],
        // ]
        foreach ($client_thread->results as $rpc => $results) {
            // initialize, can always start from scratch here
            $rpcs_by_method[$rpc] = [];
            for ($i = $start_id; $i < $end_id; $i++) {
                $hostname = $results[$i];
                if ($hostname) {
                    // initialize in case we haven't seen this hostname
                    // before
                    if (!array_key_exists($hostname, $rpcs_by_method[$rpc])) {
                        $rpcs_by_method[$rpc][$hostname] = 0;
                    }
                    if (!array_key_exists($hostname, $rpcs_by_peer)) {
                        $rpcs_by_peer[$hostname] = 0;
                    }
                    // increment the remote hostname distribution histogram
                    // both by overall, and broken down per RPC
                    $rpcs_by_method[$rpc][$hostname] += 1;
                    $rpcs_by_peer[$hostname] += 1;
                } else {
                    // $num_failures here are counted per individual RPC
                    $num_failures += 1;
                }
            }
        }

        // Convert our hashmaps above into protobuf objects
        $response = new Grpc\Testing\LoadBalancerStatsResponse();
        $rpcs_by_method_map = [];
        foreach ($rpcs_by_method as $rpc => $rpcs_by_peer_per_method) {
            $rpcs_by_peer_proto_obj
                = new Grpc\Testing\LoadBalancerStatsResponse\RpcsByPeer();
            $rpcs_by_peer_proto_obj->setRpcsByPeer($rpcs_by_peer_per_method);
            $rpcs_by_method_map[$rpc] = $rpcs_by_peer_proto_obj;
        }
        $response->setRpcsByPeer($rpcs_by_peer);
        $response->setRpcsByMethod($rpcs_by_method_map);
        $response->setNumFailures($num_failures);
        return $response;
    }
}

// This client thread blindly sends a unary RPC to the server once
// every 1 / qps seconds.
class ClientThread extends Thread {
    private $server_address_;
    private $target_seconds_between_rpcs_;
    private $fail_on_failed_rpcs_;
    private $autoload_path_;
    private $TIMEOUT_US = 30 * 1e6; // 30 seconds
    public $num_results = 0;
    public $results;

    public function __construct($server_address, $qps, $fail_on_failed_rpcs,
                                $rpcs_to_send, $metadata_to_send,
                                $autoload_path) {
        $this->server_address_ = $server_address;
        $this->target_seconds_between_rpcs_ = 1.0 / $qps;
        $this->fail_on_failed_rpcs_ = $fail_on_failed_rpcs;
        $this->rpcs_to_send = explode(',', $rpcs_to_send);
        // Convert input in the form of
        //   rpc1:k1:v1,rpc2:k2:v2,rpc1:k3:v3
        // into
        //   [
        //     'rpc1' => [
        //       'k1' => 'v1',
        //       'k3' => 'v3',
        //     ],
        //     'rpc2' => [
        //       'k2' => 'v2'
        //     ],
        //   ]
        $this->metadata_to_send = [];
        if ($_all_metadata = explode(',', $metadata_to_send)) {
            foreach ($_all_metadata as $one_metadata_pair) {
                list($rpc,
                     $metadata_key,
                     $metadata_value) = explode(':', $one_metadata_pair);
                // initialize in case we haven't seen this rpc before
                if (!array_key_exists($rpc, $this->metadata_to_send)) {
                    $this->metadata_to_send[$rpc] = [];
                }
                $this->metadata_to_send[$rpc][$metadata_key]
                    = $metadata_value;
            }
        }
        $this->autoload_path_ = $autoload_path;
        $this->simple_request = new Grpc\Testing\SimpleRequest();
        $this->empty_request = new Grpc\Testing\EmptyMessage();
        $this->results = [];
        foreach ($this->rpcs_to_send as $rpc) {
            $this->results[$rpc] = [];
        }
    }

    public function sendUnaryCall($stub, $metadata) {
        return $stub->UnaryCall($this->simple_request,
                                $metadata,
                                ['timeout' => $this->TIMEOUT_US]);
    }

    public function sendEmptyCall($stub, $metadata) {
        return $stub->EmptyCall($this->empty_request,
                                $metadata,
                                ['timeout' => $this->TIMEOUT_US]);
    }

    public function run() {
        // Autoloaded classes do not get inherited in threads.
        // Hence we need to do this.
        require_once($this->autoload_path_);

        $stub = new Grpc\Testing\TestServiceClient($this->server_address_, [
            'credentials' => Grpc\ChannelCredentials::createInsecure()
        ]);
        # hrtime returns nanoseconds
        $target_next_start_us = hrtime(true) / 1000;
        while (true) {
            $now_us = hrtime(true) / 1000;
            $sleep_us = $target_next_start_us - $now_us;
            if ($sleep_us < 0) {
                $target_next_start_us =
                        $now_us + ($this->target_seconds_between_rpcs_ * 1e6);
                echo sprintf(
                    "php xds: warning, rpc takes too long to finish. "
                    . "Deficit %.1fms."
                    . "If you consistently see this, the qps is too high.\n",
                    round(abs($sleep_us / 1000), 1));
            } else {
                $target_next_start_us +=
                        ($this->target_seconds_between_rpcs_ * 1e6);
                usleep($sleep_us);
            }
            foreach ($this->rpcs_to_send as $rpc) {
                $metadata = array_key_exists(
                    $rpc, $this->metadata_to_send) ?
                          $this->metadata_to_send[$rpc] : [];
                // This copy is somehow necessary because
                // $this->metadata_to_send[$rpc] somehow becomes a
                // Volatile object, instead of an associative array.
                $metadata_array = [];
                foreach ($metadata as $key => $value) {
                    $metadata_array[$key] = [$value];
                }
                $call = null;
                if ($rpc == 'UnaryCall') {
                    $call = $this->sendUnaryCall($stub, $metadata_array);
                } else if ($rpc == 'EmptyCall') {
                    $call = $this->sendEmptyCall($stub, $metadata_array);
                } else {
                    throw new Exception("Unhandled rpc $rpc");
                }
                // the remote peer is being returned as part of the
                // initial metadata, according to the test spec
                $initial_metadata = $call->getMetadata();
                list($response, $status) = $call->wait();
                if ($status->code == Grpc\STATUS_OK &&
                    array_key_exists('hostname', $initial_metadata)) {
                    $this->results[$rpc][] = $initial_metadata['hostname'][0];
                } else {
                    if ($this->fail_on_failed_rpcs_) {
                        throw new Exception("$rpc failed with status "
                                            . $status->code);
                    }
                    $this->results[$rpc][] = "";
                }
            }
            // $num_results here is only incremented when the group of
            // all $rpcs_to_send are done.
            $this->num_results++;
        }
    }

    // This is needed for loading autoload_path in the child thread
    public function start(int $options = PTHREADS_INHERIT_ALL) {
        return parent::start(PTHREADS_INHERIT_NONE);
    }
}


// Note: num_channels are currently ignored for now
$args = getopt('', ['fail_on_failed_rpcs:', 'num_channels:',
                    'rpc:', 'metadata:',
                    'server:', 'stats_port:', 'qps:']);

$client_thread = new ClientThread($args['server'], $args['qps'],
                                  $args['fail_on_failed_rpcs'],
                                  (empty($args['rpc']) ? 'UnaryCall'
                                   : $args['rpc']),
                                  $args['metadata'],
                                  $autoload_path);
$client_thread->start();

$server = new Grpc\RpcServer();
$server->addHttp2Port('0.0.0.0:'.$args['stats_port']);
$server->handle(new LoadBalancerStatsService());
$server->run();
