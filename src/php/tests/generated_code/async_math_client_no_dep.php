<?php
/*
 *
 * Copyright 2015 gRPC authors.
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

# Fix the following line to point to your installation
# This assumes that you are using protoc 3.2.0+ and the generated stubs
# were being autoloaded via composer.

require_once(dirname(__FILE__) . '/../../lib/Grpc/AbstractCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/BidiStreamingCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/ServerStreamingCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/ClientStreamingCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/UnaryCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/BaseStub.php');

include 'vendor/autoload.php';


$client = new Math\MathClient('localhost:50052', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);

$call_count = 0;
$completed_count = 0;

function callDivAsync($client, $dividend, $divisor)
{
    $client->Div(
        new Math\DivArgs([
            'dividend' => $dividend,
            'divisor' => $divisor,
        ]),
        [] /* metadata */,
        [
            "async_callbacks" => [
                "onData" => function ($response) use ($dividend, $divisor) {
                    if ($response) {
                        echo "== " . $dividend . " / " . $divisor .
                            ' = ' . $response->getQuotient() .
                            ' R ' . $response->getRemainder() . PHP_EOL;
                    }
                },
                "onStatus" => function ($status) use ($dividend, $divisor) {
                    if ($status->code != Grpc\STATUS_OK) {
                        echo "== " . $dividend . " / " . $divisor .
                            " = ERROR: " . $status->code .
                            ": " . $status->details . PHP_EOL;
                    }
                    global $completed_count;
                    $completed_count++;
                },
            ],
        ]
    );
}

function callFibAsync($client, $limit)
{
    $client->Fib(
        new Math\FibArgs(['limit' => $limit]),
        [] /* metadata */,
        [
            "async_callbacks" => [
                "onData" => function ($response) use ($limit) {
                    if ($response) {
                        echo "== Fib(" . $limit . ")" .
                            ": " . $response->getNum() . PHP_EOL;
                    }
                },
                "onStatus" => function ($status) use ($limit) {
                    if ($status->code != Grpc\STATUS_OK) {
                        echo "== Fib(" . $limit . ")" .
                            " = ERROR: " . $status->code .
                            ": " . $status->details . PHP_EOL;
                    }
                    global $completed_count;
                    $completed_count++;
                },
            ]
        ]
    );
}

function callSumAsync($client, ...$nums)
{
    $call = $client->Sum([]/* metadata */, [
        "async_callbacks" => [
            "onData" => function ($response) use ($nums) {
                if ($response) {
                    echo "== Sum(";
                    foreach ($nums as $num) {
                        echo $num . ",";
                    }
                    echo ") = " . $response->getNum() . PHP_EOL;
                }
            },
            "onStatus" => function ($status) use ($limit) {
                if ($status->code != Grpc\STATUS_OK) {
                    echo "== Sum(...) = ERROR: " . $status->code .
                        ": " . $status->details . PHP_EOL;
                }
                global $completed_count;
                $completed_count++;
            },
        ]
    ]);
    foreach ($nums as $num) {
        $call->write(new Math\Num(['num' => $num]));
        drainCompletionEvents();
    }
    $call->writesDone();
    drainCompletionEvents();
}

function callDivManyAsync($client, ...$divArgs)
{
    $call = $client->DivMany([] /* metadata */, [
        'async_callbacks' => [
            'onData' => function ($response) {
                if ($response) {
                    echo "== DivMany(...) = " . $response->getQuotient() .
                        " R " . $response->getRemainder() . PHP_EOL;
                }
            },
            'onStatus' => function ($status) {
                if ($status->code != Grpc\STATUS_OK) {
                    echo "== DivMany(...) = ERROR: " . $status->code .
                        ": " . $status->details . PHP_EOL;
                }
                global $completed_count;
                $completed_count++;
            },
        ],
    ]);

    foreach ($divArgs as $divArg) {
        $call->write(new Math\DivArgs([
            'dividend' => $divArg[0],
            'divisor' => $divArg[1],
        ]));
        drainCompletionEvents();
    }
    $call->writesDone();
    drainCompletionEvents();
}


$call_count = 4;
callDivAsync($client, 1000, 33);
callSumAsync($client, 1, 3, 5, 7, 9);
callFibAsync($client, 7);
callDivManyAsync($client, [10, 1], [10, 2], [10, 3]);


// wait for all async call to complete
for (;;) {
    drainCompletionEvents();
    global $completed_count;
    global $call_count;
    if ($completed_count == $call_count) {
        break;
    }
    usleep(10000);
}
