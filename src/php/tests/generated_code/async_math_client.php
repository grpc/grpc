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

function outputDivResult($dividend, $divisor, $quotient, $remainder, $status)
{
    echo "== " . $dividend . " / " . $divisor . ' = ' .
        $quotient . ' R ' . $remainder .
        '. status detail = ' . $status->details . PHP_EOL;
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
            "async_callback" => function (
                $error,
                $event
            ) use ($dividend, $divisor) {
                if ($error) {
                    echo "== " . $dividend . " / " . $divisor .
                        ": error: " . $error . PHP_EOL;
                } else {
                    outputDivResult(
                        $dividend,
                        $divisor,
                        $event->response->getQuotient(),
                        $event->response->getRemainder(),
                        $event->status
                    );
                }
                global $completed_count;
                $completed_count++;
            },
        ]
    );
}

function callDivSync($client, $dividend, $divisor)
{
    list($response, $status) = $client->Div(
        new Math\DivArgs([
            'dividend' => $dividend,
            'divisor' => $divisor,
        ])
    )->wait();
    outputDivResult(
        $dividend,
        $divisor,
        $response->getQuotient(),
        $response->getRemainder(),
        $status
    );
}

// periodicaly drain completed events until all calls completed
$w = new EvPeriodic(0., 0.05, NULL, function ($w, $revents) {
    drainCompletionEvents();
    global $completed_count;
    global $call_count;
    if ($completed_count == $call_count) {
        Ev::stop();
    }
});


echo "==== callDivAsync start " . PHP_EOL;
$now = microtime(true /* float */);
for ($i = 1; $i <= $call_count; $i++) {
    callDivAsync($client, 100, $i);
}
Ev::run();
echo "==== callDivAsync end, " . $call_count . " calls elapsed: " .
    (microtime(true /* float */) - $now) . PHP_EOL;


echo "==== callDivSync start " . PHP_EOL;
$now = microtime(true /* float */);
for ($i = 1; $i <= 10; $i++) {
    callDivSync($client, 100, $i);
}
echo "==== callDivSync end, 10 calls elapsed: " .
    (microtime(true /* float */) - $now) . PHP_EOL;
