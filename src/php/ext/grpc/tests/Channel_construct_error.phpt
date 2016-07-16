--TEST--
Test new Channel() : error conditions 
--FILE--
<?php
var_dump(new Grpc\Channel());
?>
===DONE===
--EXPECTF--
%s

Warning: Grpc\Channel::__construct() expects exactly 2 parameters, %d given in %s on line %d

Fatal error: Uncaught exception 'InvalidArgumentException' with message 'Channel expects a string and an array' in %s:%d
Stack trace:
#0 %s(%d): Grpc\Channel->__construct()
#1 {main}
  thrown in %s on line %d
