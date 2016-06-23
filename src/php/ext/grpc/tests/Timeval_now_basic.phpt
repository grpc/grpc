--TEST--
Test now of Timeval : basic functionality
--FILE--
<?php
var_dump(Grpc\Timeval::now());
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#1 (0) {
}
===DONE===
