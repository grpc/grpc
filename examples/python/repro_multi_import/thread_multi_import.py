import sys
import threading
import time

import grpc  # <-- Main thread import

# We use a lock for print statements to prevent
# garbled output from the two threads printing at once.
print_lock = threading.Lock()


def locked_print(message):
    """A thread-safe print function."""
    with print_lock:
        print(message)


def worker_thread_importer():
    """
    This function will run in a separate thread and
    try to import grpc *again*.
    """
    thread_name = threading.current_thread().name
    locked_print(f"[{thread_name}] Thread started.")

    # Give the main thread a moment to print its info
    time.sleep(0.5)

    locked_print(f"[{thread_name}] Checking 'sys.modules' *before* import...")

    # This check will be True, because the main thread
    # already imported it.
    if "grpc" in sys.modules:
        locked_print(f"[{thread_name}] 'grpc' is *already* in sys.modules.")
    else:
        # This line should never be reached
        locked_print(f"[{thread_name}] 'grpc' is NOT in sys.modules.")

    locked_print(f"[{thread_name}] Executing 'import grpc'...")

    # --- The "Second" Import ---
    # This is just a fast lookup in the sys.modules cache.
    # The grpc module's code is NOT executed again.
    import grpc

    locked_print(f"[{thread_name}] 'import grpc' is complete.")

    # --- The Proof ---
    # We print the memory address (id) of the module.
    # It will be the same as the one in the main thread.
    locked_print(f"[{thread_name}] Module ID: {id(grpc)}")
    locked_print(f"[{thread_name}] Thread finished.")


# --- Main Program Execution ---
if __name__ == "__main__":
    main_name = threading.current_thread().name
    locked_print(f"[{main_name}] Program started.")

    # We imported 'grpc' at the top level (line 4)
    locked_print(f"[{main_name}] 'grpc' was imported at the top level.")
    locked_print(f"[{main_name}] Module ID: {id(grpc)}")

    locked_print(f"[{main_name}] Starting worker thread...")

    # Create and start the new thread
    worker = threading.Thread(
        target=worker_thread_importer, name="WorkerThread"
    )
    worker.start()

    # Wait for the worker thread to complete
    worker.join()

    locked_print(f"[{main_name}] Program finished.")
