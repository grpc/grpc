<?php
class CompletionQueueTest extends PHPUnit_Framework_TestCase{
  public function testNextReturnsNullWithNoCall() {
    $cq = new Grpc\CompletionQueue();
    $event = $cq->next(Grpc\Timeval::zero());
    $this->assertNull($event);
  }

  public function testPluckReturnsNullWithNoCall() {
    $cq = new Grpc\CompletionQueue();
    $event = $cq->pluck(0, Grpc\Timeval::zero());
    $this->assertNull($event);
  }
}