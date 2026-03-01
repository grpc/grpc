import os
import sys
import time

# --- NO 'import grpc' in the parent ---

print(f"--- Parent (PID: {os.getpid()}) ---")
print(f"Parent: 'grpc' is in sys.modules? {'grpc' in sys.modules}")

num_children = 3
child_pids = []

print(f"Parent: Forking {num_children} children...")
sys.stdout.flush()  # Flush print buffer before forking

for i in range(num_children):
    try:
        pid = os.fork()
    except OSError as e:
        print(f"Parent: os.fork() failed: {e}")
        continue

    if pid == 0:
        # --- This is the Child Process ---
        child_pid = os.getpid()
        print(f"\n  --- Child {i+1} (PID: {child_pid}) ---")
        print(f"  Child {i+1}: Started. 'grpc' is not in sys.modules.")

        # Each child imports grpc for itself
        import grpc

        print(f"  Child {i+1}: 'grpc' imported.")
        print(
            f"  Child {i+1}: Your 'once-only' function ran here (Call #1 *in this child*)."
        )
        print(f"  Child {i+1}: Exiting.")

        # Use os._exit() in children after a fork to prevent
        # running the parent's cleanup code
        os._exit(0)

    else:
        # --- This is the Parent Process ---
        # Parent just records the child's PID
        child_pids.append(pid)

# --- Parent waits for all children to finish ---
print(f"\nParent: Waiting for all {len(child_pids)} children to exit...")

for pid in child_pids:
    try:
        # Wait for the specific child PID to exit
        exited_pid, exit_status = os.waitpid(pid, 0)

        if os.WIFEXITED(exit_status):
            print(f"Parent: Child {exited_pid} exited cleanly.")
        else:
            print(f"Parent: Child {exited_pid} exited abnormally.")
    except OSError as e:
        print(f"Parent: Error waiting for {pid}: {e}")

print("Parent: Test complete.")
