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

class XdsUpdateClientConfigureService
        extends \Grpc\Testing\XdsUpdateClientConfigureServiceStub
{
    function configure(
        \Grpc\Testing\ClientConfigureRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\ClientConfigureResponse {
        $rpc_types = $request->getTypes();
        $all_metadata = $request->getMetadata();
        $rpcs_to_send = [];
        foreach ($rpc_types as $rpc_type) {
            if ($rpc_type ==
                \Grpc\Testing\ClientConfigureRequest\RpcType::EMPTY_CALL) {
                $rpcs_to_send[] = 'EmptyCall';
            } else if ($rpc_type ==
                       \Grpc\Testing\ClientConfigureRequest\RpcType::UNARY_CALL) {
                $rpcs_to_send[] = 'UnaryCall';
            }
        }
        $metadata_to_send = [];
        foreach ($all_metadata as $metadata) {
            $rpc_type = $metadata->getType();
            if ($rpc_type ==
                \Grpc\Testing\ClientConfigureRequest\RpcType::EMPTY_CALL) {
                $rpc_type_key = 'EmptyCall';
            } else if ($rpc_type ==
                       \Grpc\Testing\ClientConfigureRequest\RpcType::UNARY_CALL) {
                $rpc_type_key = 'UnaryCall';
            }
            $key = $metadata->getKey();
            $value = $metadata->getValue();
            if (!isset($metadata_to_send[$rpc_type_key])) {
                $metadata_to_send[$rpc_type_key] = [];
            }
            $metadata_to_send[$rpc_type_key][$key] = $value;
        }
        global $client_thread;
        echo "PHP parent: Setting client_thread rpc_config to \n";
        print_r($rpcs_to_send);
        print_r($metadata_to_send);
        echo "PHP parent: timeout_sec = ".$request->getTimeoutSec()."\n";
        $client_thread->rpc_config->update($rpcs_to_send,
                                           $metadata_to_send,
                                           $request->getTimeoutSec());
        return new Grpc\Testing\ClientConfigureResponse();
    }
}

// The main xds interop test runner will ping this service to ask for
// the stats of the distribution of the backends, for the next X rpcs.
class LoadBalancerStatsService
    extends \Grpc\Testing\LoadBalancerStatsServiceStub
{
    function getClientStats(
        \Grpc\Testing\LoadBalancerStatsRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\LoadBalancerStatsResponse {
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
        foreach ((array)$client_thread->rpc_config->rpcs_to_send as $rpc) {
            $results = $client_thread->results[$rpc];
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

    function GetClientAccumulatedStats(
        \Grpc\Testing\LoadBalancerAccumulatedStatsRequest $request,
        \Grpc\ServerContext $context
    ): ?\Grpc\Testing\LoadBalancerAccumulatedStatsResponse {
        global $client_thread;
        $response = new Grpc\Testing\LoadBalancerAccumulatedStatsResponse();
        $response->setNumRpcsStartedByMethod(
            (array)$client_thread->num_rpcs_started_by_method);
        $response->setNumRpcsSucceededByMethod(
            (array)$client_thread->num_rpcs_succeeded_by_method);
        $response->setNumRpcsFailedByMethod(
            (array)$client_thread->num_rpcs_failed_by_method);
        $accumulated_method_stats
            = (array)$client_thread->accumulated_method_stats;
        $stats_per_method = [];
        foreach ($accumulated_method_stats as $rpc_name => $stats) {
            $methodStats
                = new Grpc\Testing\LoadBalancerAccumulatedStatsResponse\MethodStats();
            $methodStats->setRpcsStarted($stats['rpcs_started']);
            $methodStats->setResult((array)$stats['result']);
            $stats_per_method[$rpc_name] = $methodStats;
        }
        $response->setStatsPerMethod($stats_per_method);
        return $response;
    }
}

class RpcConfig extends Volatile {
    public $server_address;
    public $qps;
    public $fail_on_failed_rpcs;
    public $rpcs_to_send;
    public $metadata_to_send;
    public $tmp_file1;
    public $tmp_file2;
    public $timeout_sec;
    public function __construct($server_address,
                                $qps,
                                $fail_on_failed_rpcs,
                                $rpcs_to_send,
                                $metadata_to_send,
                                $tmp_file1,
                                $tmp_file2) {
        $this->server_address = $server_address;
        $this->qps = $qps;
        $this->fail_on_failed_rpcs = $fail_on_failed_rpcs;
        $this->rpcs_to_send = (array)$rpcs_to_send;
        $this->metadata_to_send = (array)$metadata_to_send;
        $this->tmp_file1 = $tmp_file1;
        $this->tmp_file2 = $tmp_file2;
        $this->timeout_sec = 30;
    }
    public function update($rpcs_to_send, $metadata_to_send, $timeout_sec) {
        $this->rpcs_to_send = (array)$rpcs_to_send;
        $this->metadata_to_send = (array)$metadata_to_send;
        $this->timeout_sec = $timeout_sec;
    }
}

// This client thread blindly sends a unary RPC to the server once
// every 1 / qps seconds.
class ClientThread extends Thread {
    private $target_seconds_between_rpcs_;
    private $autoload_path_;
    private $TIMEOUT_US = 30 * 1e6; // 30 seconds
    public $rpc_config;
    public $num_results = 0;
    public $results;

    public $RPC_MAP = [
        'UnaryCall' => 'UNARY_CALL',
        'EmptyCall' => 'EMPTY_CALL',
    ];

    public $num_rpcs_started_by_method = [];
    public $num_rpcs_succeeded_by_method = [];
    public $num_rpcs_failed_by_method = [];
    public $accumulated_method_stats = [];

    public function __construct($rpc_config,
                                $autoload_path) {
        $this->rpc_config = $rpc_config;
        $this->target_seconds_between_rpcs_ = 1.0 / $rpc_config->qps;
        $this->autoload_path_ = $autoload_path;
        $this->simple_request = new Grpc\Testing\SimpleRequest();
        $this->empty_request = new Grpc\Testing\EmptyMessage();
        $this->results = [];
        foreach (['UnaryCall', 'EmptyCall'] as $rpc) {
            $this->results[$rpc] = [];
        }
        $this->outstanding_rpcs = [];
        foreach (['UNARY_CALL', 'EMPTY_CALL'] as $rpc_stats_key) {
            $this->num_rpcs_started_by_method[$rpc_stats_key] = 0;
            $this->num_rpcs_succeeded_by_method[$rpc_stats_key] = 0;
            $this->num_rpcs_failed_by_method[$rpc_stats_key] = 0;
            $this->accumulated_method_stats[$rpc_stats_key] = [
                'rpcs_started' => 0,
                'result' => [],
            ];
        }
    }

    public function sendUnaryCall($stub, $metadata) {
        $timeout = $this->rpc_config->timeout_sec ?
                 $this->rpc_config->timeout_sec * 1e6 :
                 $this->TIMEOUT_US;
        return $stub->UnaryCall($this->simple_request,
                                $metadata,
                                ['timeout' => $timeout]);
    }

    public function sendEmptyCall($stub, $metadata) {
        $timeout = $this->rpc_config->timeout_sec ?
                 $this->rpc_config->timeout_sec * 1e6 :
                 $this->TIMEOUT_US;
        return $stub->EmptyCall($this->empty_request,
                                $metadata,
                                ['timeout' => $timeout]);
    }

    public function add_rpc_result($rpc, $status_code) {
        // $rpc here needs to be in the format of 'UnaryCall', 'EmptyCall'
        if (!isset($this->accumulated_method_stats[$this->RPC_MAP[$rpc]]
                   ['result'][$status_code])) {
            $this->accumulated_method_stats[$this->RPC_MAP[$rpc]]
                ['result'][$status_code] = 0;
        }
        $this->accumulated_method_stats[$this->RPC_MAP[$rpc]]
            ['result'][$status_code] += 1;
    }

    public function check_child_process_result() {
        if (sizeof($this->outstanding_rpcs) > 0 &&
            $this->rpc_config->tmp_file2) {
            $keys_to_delete = [];
            // tmp_file2 contains the RPC result of each RPC we
            // originally wrote to tmp_file1
            $f2 = fopen($this->rpc_config->tmp_file2, 'r+');
            flock($f2, LOCK_EX);
            while (true) {
                $f2_line = fgets($f2);
                if (!$f2_line) {
                    break;
                }
                // format here needs to be in sync with
                // src/php/bin/xds_manager.py
                $parts = explode(',', trim($f2_line));
                $key = $parts[0];
                $returncode = $parts[1];
                if (isset($this->outstanding_rpcs[$key])) {
                    $parts2 = explode('|', $key);
                    $result_num = $parts2[0];
                    $rpc_name = $parts2[1];
                    // Child processes can only communicate back the
                    // status code for now.
                    // Current interop test specs only call for
                    // reporting back the status code in these scenarios.
                    // If we ever need the hostname reported back from
                    // child processes, we need to enhance this
                    // communication framework through tmp files.
                    $this->results[$rpc_name][$result_num] = "";
                    if ($returncode) {
                        $this->num_rpcs_failed_by_method
                            [$this->RPC_MAP[$rpc_name]] += 1;
                    } else {
                        $this->num_rpcs_succeeded_by_method
                            [$this->RPC_MAP[$rpc_name]] += 1;
                    }
                    $this->add_rpc_result($rpc_name, $returncode);
                    $keys_to_delete[] = $key;
                }
            }
            foreach ($keys_to_delete as $key) {
                unset($this->outstanding_rpcs[$key]);
            }
            ftruncate($f2, 0);
            flock($f2, LOCK_UN);
            fclose($f2);
        }
    }

    public function execute_rpc_in_child_process($rpc, $metadata_serialized) {
        // tmp_file1 contains the list of RPCs (and their
        // specs) we want executed. This will be picked up
        // by src/php/bin/xds_manager.py
        $f1 = fopen($this->rpc_config->tmp_file1, 'a');
        $key = implode('|', [$this->num_results,
                             $rpc,
                             $metadata_serialized,
                             $this->rpc_config->timeout_sec]);
        flock($f1, LOCK_EX);
        fwrite($f1, $key."\n");
        fflush($f1);
        flock($f1, LOCK_UN);
        fclose($f1);
        $this->outstanding_rpcs[$key] = 1;
        $this->num_rpcs_started_by_method[$this->RPC_MAP[$rpc]] += 1;
        $this->accumulated_method_stats[$this->RPC_MAP[$rpc]]
            ['rpcs_started'] += 1;
    }

    public function run() {
        // Autoloaded classes do not get inherited in threads.
        // Hence we need to do this.
        require_once($this->autoload_path_);

        $stub = new Grpc\Testing\TestServiceClient(
            $this->rpc_config->server_address,
            ['credentials' => Grpc\ChannelCredentials::createInsecure()
        ]);
        // hrtime returns nanoseconds
        $target_next_start_us = hrtime(true) / 1000;
        while (true) {
            $now_us = hrtime(true) / 1000;
            $sleep_us = $target_next_start_us - $now_us;
            if ($sleep_us < 0) {
                $target_next_start_us =
                        $now_us + ($this->target_seconds_between_rpcs_ * 1e6);
            } else {
                $target_next_start_us +=
                        ($this->target_seconds_between_rpcs_ * 1e6);
                usleep($sleep_us);
            }
            $this->check_child_process_result();
            foreach ($this->rpc_config->rpcs_to_send as $rpc) {
                $metadata_to_send_arr
                    = (array)$this->rpc_config->metadata_to_send;
                $metadata = array_key_exists($rpc, $metadata_to_send_arr) ?
                          $metadata_to_send_arr[$rpc] : [];
                // This copy is somehow necessary because
                // $this->metadata_to_send[$rpc] somehow becomes a
                // Volatile object, instead of an associative array.
                $metadata_array = [];
                $execute_in_child_process = false;
                foreach ($metadata as $key => $value) {
                    $metadata_array[$key] = [$value];
                    if ($key == 'rpc-behavior' || $key == 'fi_testcase') {
                        $execute_in_child_process = true;
                    }
                }
                if ($execute_in_child_process && $this->rpc_config->tmp_file1) {
                    // if 'rpc-behavior' is set, we need to pawn off
                    // the execution to some other child PHP processes
                    $this->execute_rpc_in_child_process(
                        $rpc, serialize($metadata_array));
                    continue;
                }
                // Execute RPC within this script
                $call = null;
                if ($rpc == 'UnaryCall') {
                    $call = $this->sendUnaryCall($stub, $metadata_array);
                } else if ($rpc == 'EmptyCall') {
                    $call = $this->sendEmptyCall($stub, $metadata_array);
                } else {
                    throw new Exception("Unhandled rpc $rpc");
                }
                $this->num_rpcs_started_by_method[$this->RPC_MAP[$rpc]] += 1;
                $this->accumulated_method_stats[$this->RPC_MAP[$rpc]]
                    ['rpcs_started'] += 1;
                // the remote peer is being returned as part of the
                // initial metadata, according to the test spec
                $initial_metadata = $call->getMetadata();
                list($response, $status) = $call->wait();
                if ($status->code == Grpc\STATUS_OK &&
                    array_key_exists('hostname', $initial_metadata)) {
                    $this->results[$rpc][$this->num_results]
                        = $initial_metadata['hostname'][0];
                    $this->num_rpcs_succeeded_by_method
                        [$this->RPC_MAP[$rpc]] += 1;
                    $this->add_rpc_result($rpc, 0);
                } else {
                    if ($this->rpc_config->fail_on_failed_rpcs_) {
                        throw new Exception("$rpc failed with status "
                                            . $status->code);
                    }
                    $this->results[$rpc][$this->num_results] = "";
                    $this->num_rpcs_failed_by_method
                        [$this->RPC_MAP[$rpc]] += 1;
                    $this->add_rpc_result($rpc, $status->code);
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
                    'rpc:', 'metadata:', 'tmp_file1:', 'tmp_file2:',
                    'server:', 'stats_port:', 'qps:']);

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
$metadata_to_send = [];
if ($_all_metadata = explode(',', $args['metadata'])) {
    foreach ($_all_metadata as $one_metadata_pair) {
        list($rpc,
             $metadata_key,
             $metadata_value) = explode(':', $one_metadata_pair);
        // initialize in case we haven't seen this rpc before
        if (!array_key_exists($rpc, $metadata_to_send)) {
            $metadata_to_send[$rpc] = [];
        }
        $metadata_to_send[$rpc][$metadata_key] = $metadata_value;
    }
}
$rpcs_to_send = (empty($args['rpc']) ? 'UnaryCall' : $args['rpc']);

// Need to communicate the xds server name to the async runner manager
if ($args['tmp_file1']) {
    $f1 = fopen($args['tmp_file1'], 'w');
    fwrite($f1, 'server_address,'.$args['server']);
    fclose($f1);
}

$rpc_config = new RpcConfig($args['server'],
                            $args['qps'],
                            $args['fail_on_failed_rpcs'],
                            explode(',', $rpcs_to_send),
                            $metadata_to_send,
                            $args['tmp_file1'],
                            $args['tmp_file2']);


$client_thread = new ClientThread($rpc_config,
                                  $autoload_path);
$client_thread->start();

$server = new Grpc\RpcServer();
$server->addHttp2Port('0.0.0.0:'.$args['stats_port']);
$server->handle(new LoadBalancerStatsService());
$server->handle(new XdsUpdateClientConfigureService());
$server->run();
