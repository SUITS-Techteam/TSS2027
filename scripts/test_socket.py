from socket import *
import time

if __name__ == "__main__":
    while True:
        serverSocket = socket(AF_INET, SOCK_DGRAM)
        serverSocket.bind(('0.0.0.0', 0))
        server_address = ("172.20.126.78", 14151)
        data = bytearray([0,0,0,0,0,0,0,0])
        serverSocket.sendto(data, server_address)
        print("sent!")
        data = serverSocket.recv(9999)
        print(f"Received: {data.decode(errors='replace')}")
        serverSocket.close()
        time.sleep(1)

