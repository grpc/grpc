<?php
/*
 *
 * Copyright 2015, Google Inc.
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

# Fix the following two lines to point to your installation
include 'vendor/autoload.php';
include 'tests/generated_code/math.php';

function p($line)
{
    print("$line<br/>\n");
}

$host = 'localhost:50051';
p("Connecting to host: $host");
$client = new math\MathClient($host, []);
p('Client class: '.get_class($client));
p('');

p('Running unary call test:');
$dividend = 7;
$divisor = 4;
$div_arg = new math\DivArgs();
$div_arg->setDividend($dividend);
$div_arg->setDivisor($divisor);
$call = $client->Div($div_arg);
p('Call peer: '.$call->getPeer());
p("Dividing $dividend by $divisor");
list($response, $status) = $call->wait();
p('quotient = '.$response->getQuotient());
p('remainder = '.$response->getRemainder());
p('');

p('Running server streaming test:');
$limit = 7;
$fib_arg = new math\FibArgs();
$fib_arg->setLimit($limit);
$call = $client->Fib($fib_arg);
$result_array = iterator_to_array($call->responses());
$result = '';
foreach ($result_array as $num) {
    $result .= ' '.$num->getNum();
}
p("The first $limit Fibonacci numbers are:".$result);
p('');

p('Running client streaming test:');
$call = $client->Sum();
for ($i = 0; $i <= $limit; ++$i) {
    $num = new math\Num();
    $num->setNum($i);
    $call->write($num);
}
list($response, $status) = $call->wait();
p(sprintf('The first %d positive integers sum to: %d',
          $limit, $response->getNum()));
p('');

p('Running bidi-streaming test:');
$call = $client->DivMany();
for ($i = 0; $i < 7; ++$i) {
    $div_arg = new math\DivArgs();
    $dividend = 2 * $i + 1;
    $divisor = 3;
    $div_arg->setDividend($dividend);
    $div_arg->setDivisor($divisor);
    $call->write($div_arg);
    p("client writing: $dividend / $divisor");
    $response = $call->read();
    p(sprintf('server writing: quotient = %d, remainder = %d',
            $response->getQuotient(), $response->getRemainder()));
}
$call->writesDone();
