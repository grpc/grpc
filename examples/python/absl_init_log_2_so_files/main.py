import grpc
import hello_cython


def main():
    # Both hello_cython and grpc (via cygrpc.so) depend on absl.
    # We can try initializing logging from our dummy module.
    hello_cython.init_logging()

    print("Python Layer: Calling hello_cython...")
    hello_cython.say_hello("World from dummy .so")

    # Just to show grpc is imported and presumably its .so is loaded
    print(f"Python Layer: gRPC version is {grpc.__version__}")

    print("Python Layer: Done.")


if __name__ == "__main__":
    main()
