--TEST--
Test new Server() : error conditions 
--FILE--
<?php
var_dump(new Grpc\Server('hello'));
?>
===DONE===
--EXPECTF--
%s

Warning: Grpc\Server::__construct() expects parameter 1 to be array, string given in %s on line %d

Fatal error: Uncaught exception 'InvalidArgumentException' with message 'Server expects an array' in %s:%d
Stack trace:
#0 %s(%d): Grpc\Server->__construct('hello')
#1 {main}
  thrown in %s on line %d
