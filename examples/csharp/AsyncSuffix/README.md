# Async Suffix Example

This example demonstrates the behavior of the `append_async_suffix` option in gRPC C# code generation.

## Test Cases

1. **Basic Unary Method** (`SayHello`)
   - Generated as `SayHelloAsync` when `append_async_suffix=true`

2. **Pre-existing Async Method** (`SayHelloAsync`)
   - Doesn't get double suffix (`SayHelloAsyncAsync`)

3. **Streaming Methods**
   - Generated as `MethodNameAsync` when `append_async_suffix=true`

4. **Deprecated Method** (`OldHello`)
   - Gets Async suffix but maintains deprecated attribute