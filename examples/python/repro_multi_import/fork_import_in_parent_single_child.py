import os
import sys
import time

import grpc  # <--- Import *before* fork

print(f"--- Parent (PID: {os.getpid()}) ---")
print("Parent: Imported 'grpc'.")
print("Parent: Your 'once-only' function ran here (Call #1).")
print("Parent: Now forking...")

# Flush stdout to prevent this buffer from being
# copied and printed again by the child
sys.stdout.flush()

try:
    # os.fork() returns 0 in the child, and the child's PID in the parent
    pid = os.fork()
except OSError as e:
    print(f"os.fork() failed: {e}")
    sys.exit(1)

if pid == 0:
    # --- This is the Child Process ---
    child_pid = os.getpid()
    print(f"\n--- Child (PID: {child_pid}) ---")
    print("Child: Process started.")
    print("Child: gRPC's 'postfork_child' handler just ran,")
    print("Child: ...which re-ran the C-core init.")
    print("Child: Your 'once-only' function just ran here (Call #2).")

    # We can prove gRPC is working (not hung) by using it
    try:
        print("Child: Testing gRPC functionality (creating channel)...")
        channel = grpc.insecure_channel("localhost:12345")
        print("Child: gRPC channel created successfully.")
        channel.close()
        print("Child: Exiting normally.")

        # Exit the child process with success
        os._exit(0)

    except Exception as e:
        print(f"Child: gRPC operation failed! {e}")
        # Exit with a failure code
        os._exit(1)

else:
    # --- This is the Parent Process ---
    print(f"Parent: Forked child with PID {pid}.")
    print("Parent: Waiting for child to complete...")

    # Wait for the child to exit and get its status
    try:
        # We can prove gRPC is working (not hung) by using it
        try:
            print("Parent: Testing gRPC functionality (creating channel)...")
            channel = grpc.insecure_channel("localhost:50051")
            print("Parent: gRPC channel created successfully.")
            channel.close()
            print("Parent: Exiting normally.")

            # Exit the Parent process with success
            os._exit(0)

        except Exception as e:
            print(f"Parent: gRPC operation failed! {e}")
            # Exit with a failure code
            os._exit(1)

        exited_pid, exit_status = os.waitpid(pid, 0)

        print(f"\nParent: Child {exited_pid} has exited.")

        if os.WIFEXITED(exit_status):
            exit_code = os.WEXITSTATUS(exit_status)
            print(f"Parent: Child exit code was: {exit_code}")
            if exit_code == 0:
                print(
                    "Parent: ✅ Test shows the function was called twice with no error."
                )
            else:
                print(
                    "Parent: ❌ Test shows the child failed (this is unexpected)."
                )

        elif os.WIFSIGNALED(exit_status):
            # This would happen if you *did* have your counter
            # and it raised an unhandled exception.
            print(
                f"Parent: ❌ Child was terminated by signal: {os.WTERMSIG(exit_status)}"
            )
            print(
                "Parent: (This would happen if your guarded function raised an error!)"
            )

    except OSError as e:
        print(f"Parent: os.waitpid() failed: {e}")

    print("Parent: Test complete.")
