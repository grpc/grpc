<?php
/*
 *
 * Copyright 2016 gRPC authors.
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

include_once 'interop_client.php';

function stress_main($args)
{
    mt_srand();
    set_time_limit(0);

    // open socket to listen as metrics server
    $socket = socket_create(AF_INET, SOCK_STREAM, 0);
    socket_set_option($socket, SOL_SOCKET, SO_REUSEADDR, 1);
    if (@!socket_bind($socket, 'localhost', $args['metrics_port'])) {
        echo "Cannot create socket for metrics server...\n";
        exit(1);
    }
    socket_listen($socket);
    socket_set_nonblock($socket);

    $start_time = microtime(true);
    $count = 0;
    $deadline = $args['test_duration_secs'] ?
                ($start_time + $args['test_duration_secs']) : false;
    $num_test_cases = count($args['test_cases']);
    $stub = false;

    while (true) {
        $current_time = microtime(true);
        if ($deadline && $current_time > $deadline) {
            break;
        }
        if ($client_connection = socket_accept($socket)) {
            // there is an incoming request, respond with qps metrics
            $input = socket_read($client_connection, 1024);
            $qps = round($count / ($current_time - $start_time));
            socket_write($client_connection, "qps: $qps");
            socket_close($client_connection);
        } else {
            // do actual work, run one interop test case
            $args['test_case'] =
                $args['test_cases'][mt_rand(0, $num_test_cases - 1)];
            $stub = @interop_main($args, $stub);
            ++$count;
        }
    }
    socket_close($socket);
    echo "Number of interop tests run in $args[test_duration_secs] ".
        "seconds: $count.\n";
}

// process command line arguments
$raw_args = getopt('',
  ['server_addresses::',
   'test_cases:',
   'metrics_port::',
   'test_duration_secs::',
   'num_channels_per_server::',
   'num_stubs_per_channel::',
  ]);

$args = [];

if (empty($raw_args['server_addresses'])) {
    $args['server_host'] = 'localhost';
    $args['server_port'] = '8080';
} else {
    $parts = explode(':', $raw_args['server_addresses']);
    $args['server_host'] = $parts[0];
    $args['server_port'] = (count($parts) == 2) ? $parts[1] : '';
}

$args['metrics_port'] = empty($raw_args['metrics_port']) ?
    '8081' : $raw_args['metrics_port'];

$args['test_duration_secs'] = empty($raw_args['test_duration_secs']) ||
    $raw_args['test_duration_secs'] == -1 ?
    false : $raw_args['test_duration_secs'];

$test_cases = [];
$test_case_strs = explode(',', $raw_args['test_cases']);
foreach ($test_case_strs as $test_case_str) {
    $parts = explode(':', $test_case_str);
    $test_cases = array_merge($test_cases, array_fill(0, $parts[1], $parts[0]));
}
$args['test_cases'] = $test_cases;

stress_main($args);
