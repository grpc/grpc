--TEST--
Test new Call() : error conditions 
--FILE--
<?php
var_dump(new Grpc\Call());
?>
===DONE===
--EXPECTF--
%s

Warning: Grpc\Call::__construct() expects at least 3 parameters, 0 given in %s on line %d

Fatal error: Uncaught exception 'InvalidArgumentException' with message 'Call expects a Channel, a String, a Timeval and an optional String' in %s:%d
Stack trace:
#0 %s(%d): Grpc\Call->__construct()
#1 {main}
  thrown in %s on line %d
