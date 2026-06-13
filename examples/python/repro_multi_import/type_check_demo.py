import sys
import typing

# --- 1. Conditional Import for Type Checking ---

# This 'if' block is *False* at runtime, so the code inside
# is never executed by the Python interpreter.
# However, static type checkers (like mypy) are programmed
# to read and execute the code inside this block.
if typing.TYPE_CHECKING:
    print(">>> (This line will only print when mypy runs, not python)")
    # Import heavy modules here for type hints
    import grpc
    from grpc._channel import Channel  # A specific type from grpc


# --- 2. Function with Forward Reference Type Hint ---


# We use a string "Channel" as the type hint. This is called a
# "forward reference". It tells the type checker: "The type for
# this argument is named 'Channel', but it might not be defined *yet*."
#
# This is necessary because at runtime, the `if typing.TYPE_CHECKING:`
# block was skipped, so the name 'Channel' doesn't exist.
def check_channel(channel: "Channel") -> bool:
    """
    Checks if the provided object is a gRPC Channel.
    Demonstrates lazy-importing for runtime logic.
    """
    print("\n--- Inside check_channel ---")

    # --- 3. Runtime Import ---

    # Because the 'if' block was skipped, we must import 'grpc'
    # here to use it in our actual program logic.
    # This is often called a "lazy import" or "local import".
    print("Importing 'grpc' for runtime logic...")
    import grpc

    # Now we can safely use the 'grpc' module at runtime
    is_valid = isinstance(channel, grpc.Channel)
    print(f"Object is a real grpc.Channel: {is_valid}")
    return is_valid


# --- 4. Demonstration ---

if __name__ == "__main__":
    print("--- Running script with Python ---")

    # Check if 'grpc' is in sys.modules *before* the function call
    # It shouldn't be (unless imported by another module)
    print(f"Is 'grpc' in sys.modules *before* call? {'grpc' in sys.modules}")

    # We must import grpc here *anyway* just to create a
    # channel object to pass to our function for the demo.
    import grpc

    print(
        f"Is 'grpc' in sys.modules *after* main import? {'grpc' in sys.modules}"
    )

    # Create a real channel object
    real_channel = grpc.insecure_channel("localhost:12345")

    # Call the function
    check_channel(real_channel)

    # Call with a fake object
    print("\nCalling with a fake object...")
    check_channel("not a channel")

    print("\n--- Script finished ---")
