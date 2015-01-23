<?php
class CallTest extends PHPUnit_Framework_TestCase{
  static $server;

  public static function setUpBeforeClass() {
    $cq = new Grpc\CompletionQueue();
    self::$server = new Grpc\Server($cq, []);
    self::$server->add_http2_port('localhost:9001');
  }

  public function setUp() {
    $this->cq = new Grpc\CompletionQueue();
    $this->channel = new Grpc\Channel('localhost:9001', []);
    $this->call = new Grpc\Call($this->channel,
                                '/foo',
                                Grpc\Timeval::inf_future());
  }

  /**
   * @expectedException LogicException
   * @expectedExceptionCode Grpc\CALL_ERROR_INVALID_FLAGS
   * @expectedExceptionMessage invoke
   */
  public function testInvokeRejectsBadFlags() {
    $this->call->invoke($this->cq, 0, 0, 0xDEADBEEF);
  }

  /**
   * @expectedException LogicException
   * @expectedExceptionCode Grpc\CALL_ERROR_NOT_ON_CLIENT
   * @expectedExceptionMessage server_accept
   */
  public function testServerAcceptFailsCorrectly() {
    $this->call->server_accept($this->cq, 0);
  }

  /* These test methods with assertTrue(true) at the end just check that the
     method calls completed without errors. PHPUnit warns for tests with no
     asserts, and this avoids that warning without changing the meaning of the
     tests */

  public function testAddEmptyMetadata() {
    $this->call->add_metadata([], 0);
    /* Dummy assert: Checks that the previous call completed without error */
    $this->assertTrue(true);
  }

  public function testAddSingleMetadata() {
    $this->call->add_metadata(['key' => 'value'], 0);
    /* Dummy assert: Checks that the previous call completed without error */
    $this->assertTrue(true);
  }

  public function testAddMultiValueMetadata() {
    $this->call->add_metadata(['key' => ['value1', 'value2']], 0);
    /* Dummy assert: Checks that the previous call completed without error */
    $this->assertTrue(true);
  }

  public function testAddSingleAndMultiValueMetadata() {
    $this->call->add_metadata(
        ['key1' => 'value1',
         'key2' => ['value2', 'value3']], 0);
    /* Dummy assert: Checks that the previous call completed without error */
    $this->assertTrue(true);
  }
}
