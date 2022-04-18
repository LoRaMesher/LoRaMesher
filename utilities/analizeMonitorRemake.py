from enum import IntFlag
from asyncio.windows_events import NULL
import os
from xmlrpc.client import Boolean
import threading
import time
import json
import matplotlib.pyplot as plt
from datetime import datetime
import pandas as pd
import random

headerPacketSize = 6
dataPacketSize = 2
dataControlPacketSize = 3

nodes = list()


def extraHeaderSize(type) -> int:
    extraSize = headerPacketSize
    if isDataPacket(type):
        extraSize += dataPacketSize

    if isControlPacket(type):
        extraSize += dataControlPacketSize

    return extraSize


def isDataPacket(type) -> bool:
    return ((PacketsType.HELLO_P & type) != PacketsType.HELLO_P)


def isControlPacket(type) -> bool:
    return not((PacketsType.HELLO_P & type) == PacketsType.HELLO_P or (PacketsType.DATA_P & type) == PacketsType.DATA_P)


class PacketsType(IntFlag):
    NEED_ACK_P = 1
    DATA_P = 2
    HELLO_P = 4
    ACK_P = 8
    XL_DATA_P = 16
    LOST_P = 32
    SYNC_P = 64

    def emptyPacketType():
        listOfP = dict()
        for data in PacketsType:
            listOfP[data.value] = list()

        return listOfP


class Packet:
    def __init__(self, id, deviceId, time, type, source, destination, payloadSize, packetSize, isSend):
        self.id = id
        self.deviceId = deviceId
        self.time = time
        self.type = type
        self.source = source
        self.destination = destination
        self.payloadSize = payloadSize
        self.packetSize = packetSize
        self.isSend = isSend

    def addVia(self, via):
        self.via = via

    def addSeq_IdAndNum(self, seq_id, num):
        self.seq_id = seq_id
        self.num = num

    def addContent(self, content):
        self.content = content

    def print(self):
        print(self.__dict__)


class Node:
    def __init__(self, fileName):
        self.fileName = fileName
        self.address = 0
        self.allPacketsNum = 0

        self.rxPackets = list()
        self.txPackets = list()

        self.allPackets = list()

    def getInformation(self):
        self.getAddr()
        self.getPackets()
        self.setJSON()

    def setJSON(self):
        self.json = {
            "Name": self.fileName,
            "Addr": self.address,
            "rxPackets": self.rxPackets,
            "txPackets": self.txPackets,
            "allPackets": self.allPackets
        }

    def printInformation(self):
        print(self.json)

    def getAddr(self):

        for line in open(self.fileName):
            if 'WiFi MAC' in line:
                self.address = line[-5:-1]
                return

    def addPacketToLists(self, p, isSend):
        if isSend:
            self.txPackets.append(p.__dict__)
        else:
            self.rxPackets.append(p.__dict__)

        self.allPackets.append(p.__dict__)

    def createPacketAndSave(self, line, id):
        lineList = LineList(line)

        date = tryParseDate(line[0])
        if date == NULL:
            print("Try parse date failed")
            return False

        typeP = findType(lineList)
        if typeP == NULL:
            print("Try parse type failed")
            return False

        isSend = findIsSend(lineList)
        size = findSize(lineList)
        packet = Packet(id, self.address, date, typeP,
                        findSrc(lineList), findDst(lineList), size - extraHeaderSize(typeP), size, str(isSend))

        if isDataPacket(typeP):
            packet.addVia(findVia(lineList))

        if isControlPacket(typeP):
            packet.addSeq_IdAndNum(findSeqId(lineList), findNum(lineList))

        self.addPacketToLists(packet, isSend)

        return True

    def getPackets(self):
        i = 0
        for line in open(self.fileName):
            listLine = line.split(" ")

            if 'Receiving LoRa packet' in line:
                self.allPacketsNum += 1
                continue

            if '> Packet ' in line:
                if self.createPacketAndSave(listLine, i):
                    i += 1


def tryParseDate(date) -> str:
    try:
        s = datetime.strptime(date, '%H:%M:%S.%f').strftime('%H:%M:%S.%f')
        return s[:-3]
    except:
        return NULL


def periodStr(dateMinStr, dateMaxStr):
    try:
        dateMin = datetime.strptime(dateMinStr, '%H:%M:%S.%f')
        dateMax = datetime.strptime(dateMaxStr, '%H:%M:%S.%f')
        div = dateMax-dateMin
        strDiv = str(div)
        return strDiv[:-3]
    except:
        return NULL


def findIsSend(lineList) -> bool:
    return (lineList.getindexdefault("send") != -1)


def findType(lineList) -> int:
    return int(lineList.getNextValue("Type:"), 2)


def findSize(lineList) -> int:
    return int(lineList.getNextValue("Size:"), base=10)


def findDst(lineList) -> hex:
    return hex(int(lineList.getNextValue("Dst:"), base=16))


def findSrc(lineList) -> hex:
    return hex(int(lineList.getNextValue("Src:"), base=16))


def findVia(lineList) -> hex:
    return hex(int(lineList.getNextValue("Via:"), base=16))


def findSeqId(lineList) -> int:
    return int(lineList.getNextValue("Seq_Id:"), base=10)


def findNum(lineList) -> int:
    return int(lineList.getNextValue("Num:"), base=10)


class LineList(list):
    def getindexdefault(self, elem):
        return self.index(elem) if elem in self else -1

    def getNextValue(self, elem):
        index = self.getindexdefault(elem)
        if index == -1:
            return -1

        return self[index + 1] if index + 1 <= len(self) else -1


def init():
    for root, dirs, files in os.walk("."):
        if(root == "."):
            for filename in files:
                if (filename.__contains__("monitor") and filename.__contains__(".txt") and filename.__contains__("COM")):
                    nodes.append(Node(filename))


threads = list()


def printGeneralInfo():
    numberOfNodes = len(nodes)

    allPackets = []
    [allPackets.extend(node.allPackets) for node in nodes]

    allPacketsReceived = []
    [allPacketsReceived.extend(node.rxPackets) for node in nodes]

    allPacketsSended = []
    [allPacketsSended.extend(node.txPackets) for node in nodes]

    allTimes = [node["time"] for node in allPackets]

    dateTimeMax = max(allTimes)
    dateTimeMin = min(allTimes)

    print(
        f"Time lapsed: {periodStr(dateTimeMin, dateTimeMax)}")

    num_all_sent = len(allPacketsSended)

    num_all_received = len(allPacketsReceived)

    sizeOfAllSentInBytes = sum([packet["packetSize"]
                                for packet in allPacketsSended])

    sizeOfAllReceivedInBytes = sum([packet["packetSize"]
                                    for packet in allPacketsReceived])

    numAllPacketsRx = num_all_sent * (numberOfNodes - 1)

    lostPackets = numAllPacketsRx - num_all_received

    allBytesRx = sizeOfAllSentInBytes * (numberOfNodes - 1)

    lostBytes = allBytesRx - sizeOfAllReceivedInBytes

    print(
        f"Number of packets sent: {num_all_sent}, Max possible Packets received: {numAllPacketsRx}, Actual: {num_all_received}. Loss: {lostPackets} packets \n" +
        f"Number of bytes sent: {sizeOfAllSentInBytes}, Max possible Bytes received: {allBytesRx} bytes, Actual: {sizeOfAllReceivedInBytes} bytes. Loss: {lostBytes} bytes \n")


def saveCv(data):
    df = pd.DataFrame(data)
    df.to_csv("data.csv")

# def saveWithoutDuplicates(data):
    # df = df.drop_duplicates(
    #     subset=['id', 'time', 'source', 'destination', 'isSend'], keep='first', inplace=False)


if __name__ == '__main__':
    init()

    numberOfNodes = len(nodes)
    print(f"Number of nodes: {numberOfNodes}")

    for node in nodes:
        tr = threading.Thread(target=node.getInformation())
        tr.start()
        threads.append(tr)

    try:
        for trs in threads:
            trs.join()

        allPackets = []
        [allPackets.extend(node.allPackets) for node in nodes]

# save in a CV

        # saveCv(allPackets)

# End save in a CVx

        printGeneralInfo()

        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        print('done')
