--TEST--
Test createInsecure of ChannelCredentials : basic functionality
--FILE--
<?php
$cred = Grpc\ChannelCredentials::createInsecure();
var_dump($cred);
?>
===DONE===
--EXPECTF--
%s
NULL
===DONE===
