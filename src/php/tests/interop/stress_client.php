<?php
/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
