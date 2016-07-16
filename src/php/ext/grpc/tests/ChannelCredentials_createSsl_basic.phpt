--TEST--
Test createSsl of ChannelCredentials : basic functionality
--FILE--
<?php
$cred = Grpc\ChannelCredentials::createSsl(null, null, null);
var_dump($cred);
?>
===DONE===
--EXPECTF--
%s
object(Grpc\ChannelCredentials)#1 (0) {
}
===DONE===
