from asyncio.windows_events import NULL
import os
from xmlrpc.client import Boolean
import threading
import time
import json
import matplotlib.pyplot as plt
from datetime import datetime

nodes = list()


class Packet:
    def __init__(self, time, type, source, destination):
        self.time = time
        self.type = type
        self.source = source
        self.destination = destination

    def addVia(self, via):
        self.via = via

    def addContent(self, content):
        self.content = content

    def print(self):
        print(self.__dict__)


class Node:
    def __init__(self, fileName):
        self.fileName = fileName
        self.address = 0

        self.rxRoutePackets = list()
        self.txRoutePackets = list()
        self.rxDataPackets = list()
        self.txDataPackets = list()
        self.allPackets = list()
        self.allPacketsNum = 0

    def getInformation(self):
        self.clean()
        self.getAddr()
        self.getPackets()
        # self.printInformation()

    def printInformation(self):
        self.json = {
            "Name": self.fileName,
            "Addr": self.address,
            "AllPackets": len(self.allPackets),
            "NumSend": len(self.txDataPackets) + len(self.txRoutePackets),
            "NumReceived": len(self.rxDataPackets) + len(self.rxRoutePackets),
            "NumDataReceived": len(self.rxDataPackets),
            "NumDataSend": len(self.txDataPackets),
            "NumRouteReceived": len(self.rxRoutePackets),
            "NumRouteSend": len(self.txRoutePackets),
        }

        print(self.json)

    def clean(self):
        with open(self.fileName, 'r+') as f:
            first = True
            found = False
            for line in f.readlines():
                if(found):
                    f.write(line)

                if(line.__contains__("SSD1306 allocation Done")):
                    if(first):
                        return
                    else:
                        f.seek(0)
                        f.write(line)

                    found = True

                elif(first):
                    first = False

            print("Cleaned txt")

    def getAddr(self):

        for line in open(self.fileName):
            if 'WiFi MAC' in line:
                self.address = line[-5:-1]
                return

    def getPackets(self):
        for line in open(self.fileName):
            if 'Receiving LoRa packet' in line:
                self.allPacketsNum += 1
                continue

            elif 'HELLO packet' in line:
                date = tryParseDate(line[0:12])
                if date == NULL:
                    continue

                rxPacket = Packet(
                    date, 4, line.split("0x")[1][0:4], "FFFF")
                self.rxRoutePackets.append(rxPacket)
                self.allPackets.append(rxPacket)
                continue

            elif 'Sending packet of type 4' in line:
                date = tryParseDate(line[0:12])
                if date == NULL:
                    continue

                txPacket = Packet(
                    date, 4, self.address, "FFFF")
                self.txRoutePackets.append(txPacket)
                self.allPackets.append(txPacket)
                continue

            elif 'Sending packet of type 3' in line:
                self.setDataPacket(line, self.txDataPackets)
                continue

            elif 'Data packet from' in line and 'destination' in line:
                self.setDataPacket(line, self.rxDataPackets)
                continue

    def setDataPacket(self, line, list):
        address = line.split("0x")
        date = tryParseDate(line[0:12])
        if date == NULL:
            return

        if(len(address) < 5):
            packet = Packet(date, 3, "XXXX", "XXXX")
        else:
            packet = Packet(date, 3, address[1][0:4], address[2][0:4])
            packet.addVia(address[3][0:4])

        list.append(packet)
        self.allPackets.append(packet)


def tryParseDate(date):
    try:
        return datetime.strptime(date, '%H:%M:%S.%f')
    except:
        return NULL


def init():
    for root, dirs, files in os.walk("."):
        if(root == "."):
            for filename in files:
                if (filename.__contains__("monitor") and filename.__contains__(".txt") and filename.__contains__("COM")):
                    nodes.append(Node(filename))


threads = list()

if __name__ == '__main__':
    init()

    numberOfNodes = len(nodes)
    print(numberOfNodes)

    for node in nodes:
        tr = threading.Thread(target=node.getInformation())
        tr.start()
        threads.append(tr)

    try:
        for trs in threads:
            trs.join()

        dateTimeMax = max(
            packet.time for packet in node.allPackets for node in nodes)

        dateTimeMin = min(
            packet.time for packet in node.allPackets for node in nodes)

        numberOfSentAll = sum(len(node.txDataPackets) +
                              len(node.txRoutePackets) for node in nodes)
        numberOfReceivedAll = sum(node.allPacketsNum for node in nodes)
        # sum(len(node.rxDataPackets) + len(node.rxRoutePackets) for node in nodes)

        print("------")
        for node in nodes:
            node.printInformation()
            print("------")

        print(
            f"MinTime: {dateTimeMin.time()}, MaxTime: {dateTimeMax.time()}, Diff: {(dateTimeMax - dateTimeMin)}")

        maxReceivedByNode = numberOfSentAll / \
            numberOfNodes * (numberOfNodes - 1)
        realReceivedByNode = numberOfReceivedAll/numberOfNodes
        #round(((maxReceptionByNode)-(numberOfReceivedAll/numberOfNodes))/numberOfSentAll * 100, 2)
        print(
            f"Sent: {numberOfSentAll}, Average Max Received by Node: {maxReceivedByNode}, Average Received by Node: {realReceivedByNode}, Loss: {round((1 - realReceivedByNode / maxReceivedByNode) * 100,2)}%")

        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        print('done')
