#!/bin/bash -e
TARGET='Greeter Client'
TARGET_CLASS='ex.grpc.GreeterClient'
TARGET_ARGS="$@"

cd "$(dirname "$0")"
mvn -q -nsu -am package -Dcheckstyle.skip=true -DskipTests
. target/bootclasspath.properties
echo "[INFO] Running: $TARGET ($TARGET_CLASS $TARGET_ARGS)"
exec java "$bootclasspath" -cp "$jar" "$TARGET_CLASS" $TARGET_ARGS
