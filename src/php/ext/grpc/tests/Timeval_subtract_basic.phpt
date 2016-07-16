--TEST--
Test subtract of Timeval : basic functionality
--FILE--
<?php
$b = new Grpc\Timeval(1234);
$e = new Grpc\Timeval(5678);
$time = $e->subtract($b);
var_dump($time);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#3 (0) {
}
===DONE===
