--TEST--
Test zero of Timeval : basic functionality
--FILE--
<?php
var_dump(Grpc\Timeval::zero());
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#1 (0) {
}
===DONE===
