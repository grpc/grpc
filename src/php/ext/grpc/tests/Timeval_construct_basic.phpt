--TEST--
Test new Timeval() : basic functionality
--FILE--
<?php
var_dump(new Grpc\Timeval(1234));
var_dump(new Grpc\Timeval(123.456));
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#1 (0) {
}
object(Grpc\Timeval)#1 (0) {
}
===DONE===
