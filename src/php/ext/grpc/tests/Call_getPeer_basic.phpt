--TEST--
Test getPeer of Call : basic functionality
--FILE--
<?php
$channel = new Grpc\Channel('localhost:1234', []);
$call = new Grpc\Call($channel, '/hello', Grpc\Timeval::infFuture());
var_dump($call->getPeer());
?>
===DONE===
--EXPECTF--
%s
string(14) "localhost:%d"
===DONE===
