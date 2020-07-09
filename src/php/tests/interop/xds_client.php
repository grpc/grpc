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
        $rpcs_by_peer = [];
        $num_failures = $num_rpcs;

        // Heavy limitation now: the server is blocking, until all
        // the necessary num_rpcs are finished, or timeout is reached
        global $client_thread;
        $start_id = count($client_thread->results) + 1;
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
            $curr_id = count($client_thread->results);
            if ($curr_id >= $end_id) {
                break;
            }
            usleep(50000);
        }

        // Tally up results
        $end_id = min($end_id, count($client_thread->results));
        for ($i = $start_id; $i < $end_id; $i++) {
            $hostname = $client_thread->results[$i];
            if ($hostname) {
                $num_failures -= 1;
                if (!array_key_exists($hostname, $rpcs_by_peer)) {
                    $rpcs_by_peer[$hostname] = 0;
                }
                $rpcs_by_peer[$hostname] += 1;
            }
        }
        $response = new Grpc\Testing\LoadBalancerStatsResponse();
        $response->setRpcsByPeer($rpcs_by_peer);
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
    public $results;
    
    public function __construct($server_address, $qps, $fail_on_failed_rpcs,
                                $autoload_path) {
        $this->server_address_ = $server_address;
        $this->target_seconds_between_rpcs_ = 1.0 / $qps;
        $this->fail_on_failed_rpcs_ = $fail_on_failed_rpcs;
        $this->autoload_path_ = $autoload_path;
        $this->results = [];
    }

    public function run() {
        // Autoloaded classes do not get inherited in threads.
        // Hence we need to do this.
        require_once($this->autoload_path_);
        $TIMEOUT_US = 30 * 1e6; // 30 seconds

        $stub = new Grpc\Testing\TestServiceClient($this->server_address_, [
            'credentials' => Grpc\ChannelCredentials::createInsecure()
        ]);
        $request = new Grpc\Testing\SimpleRequest();
        $target_next_start_us = hrtime(true) / 1000; # hrtime returns nanoseconds
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
            list($response, $status)
                = $stub->UnaryCall($request, [],
                                   ['timeout' => $TIMEOUT_US])->wait();
            if ($status->code == Grpc\STATUS_OK) {
                $this->results[] = $response->getHostname();
            } else {
                if ($this->fail_on_failed_rpcs_) {
                    throw new Exception('UnaryCall failed with status '
                                        . $status->code);
                }
                $this->results[] = "";
            }
        }
    }

    // This is needed for loading autoload_path in the child thread
    public function start(int $options = PTHREADS_INHERIT_ALL) {
        return parent::start(PTHREADS_INHERIT_NONE);
    }
}


// Note: num_channels are currently ignored for now
$args = getopt('', ['fail_on_failed_rpcs:', 'num_channels:',
                    'server:', 'stats_port:', 'qps:']);

$client_thread = new ClientThread($args['server'], $args['qps'],
                                  $args['fail_on_failed_rpcs'],
                                  $autoload_path);
$client_thread->start();

$server = new Grpc\RpcServer();
$server->addHttp2Port('0.0.0.0:'.$args['stats_port']);
$server->handle(new LoadBalancerStatsService());
$server->run();
