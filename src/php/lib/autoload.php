<?php
function grpcAutoloader($class) {
  $prefix = 'Grpc\\';

  $base_dir = __DIR__ . '/Grpc/';

  $len = strlen($prefix);
  if (strncmp($prefix, $class, $len) !== 0) {
    return;
  }

  $relative_class = substr($class, $len);

  $file = $base_dir . str_replace('\\', '/', $relative_class) . '.php';

  if (file_exists($file)) {
    include $file;
  }
}

spl_autoload_register('grpcAutoloader');