--TEST--
Test similar of Timeval : basic functionality
--FILE--
<?php
$a = new Grpc\Timeval(1234);
$b = new Grpc\Timeval(1234);
$c = new Grpc\Timeval(1500);
$d = new Grpc\Timeval(200);
var_dump(Grpc\Timeval::similar($a, $b, $c));
var_dump(Grpc\Timeval::similar($a, $c, $b));
var_dump(Grpc\Timeval::similar($c, $a, $d));
?>
===DONE===
--EXPECTF--
%s
bool(true)
bool(true)
bool(false)
===DONE===
