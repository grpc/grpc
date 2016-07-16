--TEST--
Test cancel of Call : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:1234', []);
$call = new Grpc\Call($channel, '/hello', Grpc\Timeval::infFuture());
$call->cancel();
var_dump($call);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Call)#2 (1) {
  ["channel"]=>
  object(Grpc\Channel)#1 (0) {
  }
}
===DONE===
