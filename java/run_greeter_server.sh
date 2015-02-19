#!/bin/bash -e
TARGET='Greeter Server'
TARGET_CLASS='ex.grpc.GreeterServer'

cd "$(dirname "$0")"
mvn -q -nsu -am package -Dcheckstyle.skip=true -DskipTests
. target/bootclasspath.properties
echo "[INFO] Running: $TARGET ($TARGET_CLASS)"
exec java "$bootclasspath" -cp "$jar" "$TARGET_CLASS"
