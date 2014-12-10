<?php
class TimevalTest extends PHPUnit_Framework_TestCase{
  public function testCompareSame() {
    $zero = Grpc\Timeval::zero();
    $this->assertEquals(0, Grpc\Timeval::compare($zero, $zero));
  }

  public function testPastIsLessThanZero() {
    $zero = Grpc\Timeval::zero();
    $past = Grpc\Timeval::inf_past();
    $this->assertLessThan(0, Grpc\Timeval::compare($past, $zero));
    $this->assertGreaterThan(0, Grpc\Timeval::compare($zero, $past));
  }

  public function testFutureIsGreaterThanZero() {
    $zero = Grpc\Timeval::zero();
    $future = Grpc\Timeval::inf_future();
    $this->assertLessThan(0, Grpc\Timeval::compare($zero, $future));
    $this->assertGreaterThan(0, Grpc\Timeval::compare($future, $zero));
  }

  /**
   * @depends testFutureIsGreaterThanZero
   */
  public function testNowIsBetweenZeroAndFuture() {
    $zero = Grpc\Timeval::zero();
    $future = Grpc\Timeval::inf_future();
    $now = Grpc\Timeval::now();
    $this->assertLessThan(0, Grpc\Timeval::compare($zero, $now));
    $this->assertLessThan(0, Grpc\Timeval::compare($now, $future));
  }
}