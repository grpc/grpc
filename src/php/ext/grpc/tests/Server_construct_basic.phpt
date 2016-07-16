--TEST--
Test new Server() : basic functionality
--FILE--
<?php
$server = new Grpc\Server([]);
var_dump($server);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\Server)#1 (0) {
}
===DONE===
