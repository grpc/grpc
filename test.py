from grpc._cython import cygrpc
from typeguard import typechecked

# Only typeguard detects return type issue
@typechecked
def some_function(input: int) -> cygrpc.StatusCode:
    output: str = 'abc' # Both PyType and typeguard detect this
    return output

some_function(123)
