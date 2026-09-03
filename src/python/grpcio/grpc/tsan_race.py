import ctypes
import threading


ITERATIONS=1000
THREAD_COUNT=10


def write_value_to_buf(buffer_address, value):
    for _ in range(ITERATIONS):
        ctypes.memset(buffer_address, value, 8)


def main():
    buffer = ctypes.create_string_buffer(8)

    threads = [
        threading.Thread(
            target=write_value_to_buf,
            args=(ctypes.addressof(buffer), i))
        for i in range(THREAD_COUNT)
    ]

    for t in threads:
        t.start()
    for t in threads:
        t.join()


if __name__ == "__main__":
    main()
