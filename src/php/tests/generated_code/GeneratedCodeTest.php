<?php
require __DIR__ . '/../../lib/Grpc/ActiveCall.php';
require __DIR__ . '/../../lib/Grpc/SurfaceActiveCall.php';
require __DIR__ . '/../../lib/Grpc/BaseStub.php';
require 'DrSlump/Protobuf.php';
\DrSlump\Protobuf::autoload();
require 'math.php';
class GeneratedCodeTest extends PHPUnit_Framework_TestCase {
  /* These tests require that a server exporting the math service must be
   * running on $GRPC_TEST_HOST */
  protected static $client;
  protected static $timeout;
  public static function setUpBeforeClass() {
    self::$client = new math\MathClient(getenv('GRPC_TEST_HOST'));
  }

  public function testSimpleRequest() {
    $div_arg = new math\DivArgs();
    $div_arg->setDividend(7);
    $div_arg->setDivisor(4);
    list($response, $status) = self::$client->Div($div_arg)->wait();
    $this->assertEquals(1, $response->getQuotient());
    $this->assertEquals(3, $response->getRemainder());
    $this->assertEquals(\Grpc\STATUS_OK, $status->code);
  }

  public function testServerStreaming() {
    $fib_arg = new math\FibArgs();
    $fib_arg->setLimit(7);
    $call = self::$client->Fib($fib_arg);
    $result_array = iterator_to_array($call->responses());
    $extract_num = function($num){
      return $num->getNum();
    };
    $values = array_map($extract_num, $result_array);
    $this->assertEquals([1, 1, 2, 3, 5, 8, 13], $values);
    $status = $call->getStatus();
    $this->assertEquals(\Grpc\STATUS_OK, $status->code);
  }

  public function testClientStreaming() {
    $num_iter = function() {
      for ($i = 0; $i < 7; $i++) {
        $num = new math\Num();
        $num->setNum($i);
        yield $num;
      }
    };
    $call = self::$client->Sum($num_iter());
    list($response, $status) = $call->wait();
    $this->assertEquals(21, $response->getNum());
    $this->assertEquals(\Grpc\STATUS_OK, $status->code);
  }

  public function testBidiStreaming() {
    $call = self::$client->DivMany();
    for ($i = 0; $i < 7; $i++) {
      $div_arg = new math\DivArgs();
      $div_arg->setDividend(2 * $i + 1);
      $div_arg->setDivisor(2);
      $call->write($div_arg);
      $response = $call->read();
      $this->assertEquals($i, $response->getQuotient());
      $this->assertEquals(1, $response->getRemainder());
    }
    $call->writesDone();
    $status = $call->getStatus();
    $this->assertEquals(\Grpc\STATUS_OK, $status->code);
  }
}