import multiprocessing
import os
import sys
import time

# --- NO 'import grpc' in the global scope ---


def child_worker(child_num):
    """
    This function is the target for each child process.
    """
    child_pid = os.getpid()
    print(f"\n  --- Child {child_num} (PID: {child_pid}) ---")
    print(f"  Child {child_num}: Started. 'grpc' is not in sys.modules.")

    # Each child imports grpc for itself
    import grpc

    print(f"  Child {child_num}: 'grpc' imported.")
    print(
        f"  Child {child_num}: Your 'once-only' function ran here (Call #1 *in this child*)."
    )

    # Do some minimal work to simulate a real task
    time.sleep(0.5)
    print(f"  Child {child_num}: Exiting.")


# This guard is required for 'spawn' and 'forkserver' start methods
# (default on Windows and macOS)
if __name__ == "__main__":

    print(f"--- Parent (PID: {os.getpid()}) ---")
    print(f"Parent: 'grpc' is in sys.modules? {'grpc' in sys.modules}")

    num_children = 3
    processes = []

    print(f"Parent: Starting {num_children} child processes...")

    for i in range(num_children):
        # 1. Create the process object
        p = multiprocessing.Process(target=child_worker, args=(i + 1,))
        processes.append(p)

        # 2. Start the process
        p.start()
        print(f"Parent: Started child {i+1} (PID: {p.pid}).")

    print(f"\nParent: Waiting for all {len(processes)} children to finish...")

    for p in processes:
        # 3. Join the process (wait for it to terminate)
        p.join()

    print("\nParent: All children have joined.")
    print("Parent: Test complete.")
