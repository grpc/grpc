--TEST--
Test new Timeval() : error conditions 
--FILE--
<?php
var_dump(new Grpc\Timeval('hello'));
?>
===DONE===
--EXPECTF--
%s

Warning: Grpc\Timeval::__construct() expects parameter 1 to be long, string given in %s on line %d

Fatal error: Uncaught exception 'InvalidArgumentException' with message 'Timeval expects a long' in %s:%d
Stack trace:
#0 %s(%d): Grpc\Timeval->__construct('hello')
#1 {main}
  thrown in %s on line %d
