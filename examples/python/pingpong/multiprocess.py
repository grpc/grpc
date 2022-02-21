from multiprocessing import Process
import pingpong_client as client


if __name__ == "__main__":
    number = 2
    processes = {}
    for i in range(number):
        processes[i] = Process(target=client.run)
    for i in range(number):
        processes[i].start()
