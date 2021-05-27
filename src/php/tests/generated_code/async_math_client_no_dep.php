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
require_once(dirname(__FILE__) . '/../../lib/Grpc/UnaryCall.php');
require_once(dirname(__FILE__) . '/../../lib/Grpc/BaseStub.php');

include 'vendor/autoload.php';


$client = new Math\MathClient('localhost:50052', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
]);

$call_count = 1000;
$completed_count = 0;

function outputDivResult($dividend, $divisor, $quotient, $remainder)
{
    echo "== " . $dividend . " / " . $divisor . ' = ' .
        $quotient . ' R ' . $remainder . PHP_EOL;
}

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
                        outputDivResult(
                            $dividend,
                            $divisor,
                            $response->getQuotient(),
                            $response->getRemainder(),
                        );
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

echo "==== callDivAsync start " . PHP_EOL;

$now = microtime(true /* float */);
for ($i = 1; $i <= $call_count; $i++) {
    callDivAsync($client, 1000, $i % $call_count);

    // process completion evens if any 
    drainCompletionEvents();
}

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

echo "==== callDivAsync end, " . $call_count . " calls elapsed: " .
    (microtime(true /* float */) - $now) . PHP_EOL;
