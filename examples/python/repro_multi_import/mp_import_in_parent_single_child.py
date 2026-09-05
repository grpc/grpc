import multiprocessing
import os
import sys
import time


def child_worker():
    """This function is executed by the child process."""
    child_pid = os.getpid()
    print(f"\n  --- Child (PID: {child_pid}) ---")
    print(f"  Child: Process started.")

    # On 'fork' systems, the gRPC 'at_fork' handler has
    # *already run* right after the fork, and your function
    # was called a second time.

    # On 'spawn' systems, this import will be the first
    # time gRPC is loaded in this process.

    print(
        f"  Child: 'grpc' in sys.modules *before* import? {'grpc' in sys.modules}"
    )

    import grpc

    print(f"  Child: 'grpc' import finished.")
    print(f"  Child: Your 'once-only' function was called either:")
    print(f"  Child:   a) just now (on 'spawn')")
    print(f"  Child:   b) during the 'at_fork' handler (on 'fork')")
    print(f"  Child: Exiting.")


# The 'if __name__ == "__main__":' guard is essential
if __name__ == "__main__":

    # --- Parent imports gRPC *before* starting the child ---
    print(f"--- Parent (PID: {os.getpid()}) ---")
    print("Parent: Importing 'grpc'...")

    import grpc  # <--- Parent import

    print("Parent: 'grpc' imported.")
    print("Parent: Your 'once-only' function ran here (Call #1).")

    # We explicitly set the start method to 'fork' to test this
    # scenario, as it's the more complex one.
    try:
        multiprocessing.set_start_method("fork")
        print("Parent: Start method set to 'fork'.")
    except (ValueError, AttributeError, RuntimeError):
        print("Parent: Could not set start method to 'fork'.")
        print("Parent: Continuing with default (likely 'spawn').")

    print("\nParent: Starting child process...")
    p = multiprocessing.Process(target=child_worker)
    p.start()

    # Wait for the child to finish
    p.join()

    print("\nParent: Child process has joined.")
    print("Parent: Test complete.")
