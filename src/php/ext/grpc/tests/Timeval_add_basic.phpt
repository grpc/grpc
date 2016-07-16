--TEST--
Test add of Timeval : basic functionality
--FILE--
<?php
$b = new Grpc\Timeval(1000);
$e = new Grpc\Timeval(1000);
$time = $e->add($b);
var_dump($time);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#3 (0) {
}
===DONE===
