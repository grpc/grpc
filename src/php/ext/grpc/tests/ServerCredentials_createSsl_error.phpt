--TEST--
Test createSsl of ServerCredentials : error conditions 
--FILE--
<?php
var_dump(Grpc\ServerCredentials::createSsl());
?>
===DONE===
--EXPECTF--
%s

Warning: Grpc\ServerCredentials::createSsl() expects exactly 3 parameters, %d given in %s on line %d

Fatal error: Uncaught exception 'InvalidArgumentException' with message 'createSsl expects 3 strings' in %s:%d
Stack trace:
#0 %s(%d): Grpc\ServerCredentials::createSsl()
#1 {main}
  thrown in %s on line %d
