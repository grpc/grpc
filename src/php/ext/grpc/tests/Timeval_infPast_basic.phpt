--TEST--
Test infPast of Timeval : basic functionality
--FILE--
<?php
var_dump(Grpc\Timeval::infPast());
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Timeval)#1 (0) {
}
===DONE===
