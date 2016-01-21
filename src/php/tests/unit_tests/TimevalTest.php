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
class TimevalTest extends PHPUnit_Framework_TestCase
{
    public function testCompareSame()
    {
        $zero = Grpc\Timeval::zero();
        $this->assertSame(0, Grpc\Timeval::compare($zero, $zero));
    }

    public function testPastIsLessThanZero()
    {
        $zero = Grpc\Timeval::zero();
        $past = Grpc\Timeval::infPast();
        $this->assertLessThan(0, Grpc\Timeval::compare($past, $zero));
        $this->assertGreaterThan(0, Grpc\Timeval::compare($zero, $past));
    }

    public function testFutureIsGreaterThanZero()
    {
        $zero = Grpc\Timeval::zero();
        $future = Grpc\Timeval::infFuture();
        $this->assertLessThan(0, Grpc\Timeval::compare($zero, $future));
        $this->assertGreaterThan(0, Grpc\Timeval::compare($future, $zero));
    }

    /**
     * @depends testFutureIsGreaterThanZero
     */
    public function testNowIsBetweenZeroAndFuture()
    {
        $zero = Grpc\Timeval::zero();
        $future = Grpc\Timeval::infFuture();
        $now = Grpc\Timeval::now();
        $this->assertLessThan(0, Grpc\Timeval::compare($zero, $now));
        $this->assertLessThan(0, Grpc\Timeval::compare($now, $future));
    }

    public function testNowAndAdd()
    {
        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(1000);
        $deadline = $now->add($delta);
        $this->assertGreaterThan(0, Grpc\Timeval::compare($deadline, $now));
    }

    public function testNowAndSubtract()
    {
        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(1000);
        $deadline = $now->subtract($delta);
        $this->assertLessThan(0, Grpc\Timeval::compare($deadline, $now));
    }

    public function testAddAndSubtract()
    {
        $now = Grpc\Timeval::now();
        $delta = new Grpc\Timeval(1000);
        $deadline = $now->add($delta);
        $back_to_now = $deadline->subtract($delta);
        $this->assertSame(0, Grpc\Timeval::compare($back_to_now, $now));
    }
}
