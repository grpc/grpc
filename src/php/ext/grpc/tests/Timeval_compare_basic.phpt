--TEST--
Test compare of Timeval : basic functionality
--FILE--
<?php
$b = new Grpc\Timeval(1234);
$e = new Grpc\Timeval(5678);
$result = Grpc\Timeval::compare($b, $e);
var_dump($result);
var_dump(Grpc\Timeval::compare($e, $b));
var_dump(Grpc\Timeval::compare($b, new Grpc\Timeval(1234)));
?>
===DONE===
--EXPECTF--
%s
int(-1)
int(1)
int(0)
===DONE===
