--TEST--
Test sleepUntil of Timeval : basic functionality
--FILE--
<?php
$b = microtime(true);
$now = Grpc\Timeval::now();
$time = new Grpc\Timeval(100);
$a = $now->add($time);
$a->sleepUntil();
$e = microtime(true);
var_dump($e - $b);
?>
===DONE===
--EXPECTF--
%s
float(%f)
===DONE===
