#!/bin/bash
# Test script for standalone gRPC Python plugin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRPC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="$SCRIPT_DIR"
OUTPUT_DIR="$TEST_DIR/generated"

echo "Testing standalone gRPC Python plugin..."
echo "GRPC_ROOT: $GRPC_ROOT"
echo "TEST_DIR: $TEST_DIR"

# Clean output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Find the plugin binary
PLUGIN_BINARY=""
if [ -f "$GRPC_ROOT/build/grpc_python_plugin" ]; then
    PLUGIN_BINARY="$GRPC_ROOT/build/grpc_python_plugin"
elif [ -f "$GRPC_ROOT/cmake/build/grpc_python_plugin" ]; then
    PLUGIN_BINARY="$GRPC_ROOT/cmake/build/grpc_python_plugin"
elif [ -f "$GRPC_ROOT/bazel-bin/src/compiler/grpc_python_plugin_native" ]; then
    PLUGIN_BINARY="$GRPC_ROOT/bazel-bin/src/compiler/grpc_python_plugin_native"
else
    echo "ERROR: Could not find grpc_python_plugin binary"
    echo "Please build the plugin first using:"
    echo "  cmake -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=ON .. && make grpc_python_plugin"
    echo "  OR"
    echo "  bazel build //src/compiler:grpc_python_plugin"
    exit 1
fi

echo "Using plugin binary: $PLUGIN_BINARY"

# Check if protoc is available
if ! command -v protoc &> /dev/null; then
    echo "ERROR: protoc is not installed or not in PATH"
    exit 1
fi

echo "Using protoc: $(which protoc)"
echo "Protoc version: $(protoc --version)"

# Test the plugin
echo "Generating Python code with standalone plugin..."

protoc \
    --plugin="protoc-gen-grpc_python=$PLUGIN_BINARY" \
    --python_out="$OUTPUT_DIR" \
    --grpc_python_out="$OUTPUT_DIR" \
    --proto_path="$TEST_DIR" \
    "$TEST_DIR/simple_service.proto"

# Check that files were generated
echo "Checking generated files..."

EXPECTED_FILES=(
    "simple_service_pb2.py"
    "simple_service_pb2_grpc.py"
)

for file in "${EXPECTED_FILES[@]}"; do
    if [ ! -f "$OUTPUT_DIR/$file" ]; then
        echo "ERROR: Expected file $file was not generated"
        exit 1
    fi
    echo "✓ Generated: $file"
done

# Verify the generated files contain expected content
echo "Verifying generated content..."

# Check for service stub in grpc file
if ! grep -q "TestServiceStub" "$OUTPUT_DIR/simple_service_pb2_grpc.py"; then
    echo "ERROR: TestServiceStub not found in generated grpc file"
    exit 1
fi
echo "✓ Found TestServiceStub"

# Check for service servicer in grpc file  
if ! grep -q "TestServiceServicer" "$OUTPUT_DIR/simple_service_pb2_grpc.py"; then
    echo "ERROR: TestServiceServicer not found in generated grpc file"
    exit 1
fi
echo "✓ Found TestServiceServicer"

# Check for RPC methods
EXPECTED_METHODS=("SayHello" "ListNumbers" "SumNumbers" "Chat")
for method in "${EXPECTED_METHODS[@]}"; do
    if ! grep -q "$method" "$OUTPUT_DIR/simple_service_pb2_grpc.py"; then
        echo "ERROR: Method $method not found in generated grpc file"
        exit 1
    fi
    echo "✓ Found method: $method"
done

# Check for message classes in pb2 file
EXPECTED_MESSAGES=("HelloRequest" "HelloReply" "NumberRequest" "NumberReply" "SumReply" "ChatMessage")
for message in "${EXPECTED_MESSAGES[@]}"; do
    if ! grep -q "class $message" "$OUTPUT_DIR/simple_service_pb2.py"; then
        echo "ERROR: Message class $message not found in generated pb2 file"
        exit 1
    fi
    echo "✓ Found message: $message"
done

echo ""
echo "✅ SUCCESS: Standalone gRPC Python plugin test passed!"
echo ""
echo "Generated files:"
ls -la "$OUTPUT_DIR"

echo ""
echo "Plugin test completed successfully. The standalone plugin is working correctly."
echo "You can now use the plugin with protoc as follows:"
echo ""
echo "  protoc --plugin=protoc-gen-grpc_python=$PLUGIN_BINARY \\"
echo "         --grpc_python_out=./output \\"
echo "         --python_out=./output \\"
echo "         your_service.proto"
echo ""
echo "Or install as protoc-gen-grpc_python in your PATH for automatic discovery."