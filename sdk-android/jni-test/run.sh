#!/usr/bin/env bash
# Builds the bridge for the desktop JVM and runs JniTest. Usage: ./run.sh [extra cmake args]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TRI_JNI_OUT:-$here/build}"
cmake -S "$here" -B "$out" -DTRISKELION_BUILD_TESTS=OFF "$@" >/dev/null
cmake --build "$out" -j >/dev/null
mkdir -p "$out/classes"
javac -Xlint:all -d "$out/classes" "$here/../src/main/java/com/triskelion/sdk/NativeBridge.java" "$here/com/triskelion/sdk/JniTest.java"
export LC_ALL=C.UTF-8
exec java -Xcheck:jni -Djava.library.path="$out" \
  -Dtri.fixtures="$here/../../core/test/fixtures" -cp "$out/classes" com.triskelion.sdk.JniTest
