--TEST--
Check for grpc presence
--SKIPIF--
<?php if (!extension_loaded("grpc")) print "skip"; ?>
--FILE--
<?php 
echo "grpc extension is available";
?>
--EXPECT--
grpc extension is available