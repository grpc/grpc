--TEST--
Check for grpc presence
--SKIPIF--
<?hh if (!extension_loaded("grpc")) print "skip"; ?>
--FILE--
<?hh 
echo "grpc extension is available";
?>
--EXPECT--
grpc extension is available
