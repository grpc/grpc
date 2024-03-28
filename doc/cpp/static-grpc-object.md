# gRPC Objects with static storage duration

In general, it's not supported to define gRPC objects with static
storage duration because it can have a indeterministic crash due to certain
initialization order or termination order.
This is a well known issue as describied in
[Static Initialization Order Fiasco](https://en.cppreference.com/w/cpp/language/siof)
and guided in
[Static and Global Variables](https://google.github.io/styleguide/cppguide.html#Static_and_Global_Variables)

## Example

The following code may crash because `cq`, which is a `grpc::CompleitionQueue`
type is defined as a global static variable which is going to be initialized
before `main()`.
To initialize `grpc::CompletionQueue` type object, it needs fully initialized
gRPC system but it may or may not happen at that point.
In addition to that, when `cq` is destroyed, it also needs alive gRPC system
which may be terminiated already.

```
static grpc::CompletionQueue cq;

int main() {
    cq.Shutdown();
}

```

To address this problem, you can define it
with non static storage duration as follows.

```
int main() {
  grpc::CompletionQueue cq;
  cq.Shutdown();
}
```
