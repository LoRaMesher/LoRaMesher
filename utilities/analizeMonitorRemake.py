from calendar import c
from enum import IntFlag
from asyncio.windows_events import NULL
import os
import threading
import time
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
from datetime import timedelta
from datetime import datetime
import pandas as pd
import numpy as np

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

    def __eq__(self, other):
        if isinstance(other, Packet):
            selfTime = tryParseDateTime(self.time)
            minTime = selfTime-timedelta(microseconds=200)
            maxTime = selfTime+timedelta(microseconds=200)

            othersTime = tryParseDateTime(other)
            return minTime <= othersTime and maxTime >= othersTime and self.type == other.type and self.source == other.source and self.destination == other.destination and self.payloadSize == other.payloadSize and self.packetSize == other.packetSize

        return False

    def addVia(self, via):
        self.via = via

    def addSeq_IdAndNum(self, seq_id, num):
        self.seq_id = seq_id
        self.num = num

    def addContent(self, content):
        self.content = content

    def print(self):
        print(self.__dict__)


class PError:
    def __init__(self, deviceId, time):
        self.deviceId = deviceId
        self.time = time


class Node:
    def __init__(self, fileName):
        self.fileName = fileName
        self.address = 0
        self.allPacketsNum = 0

        self.rxPackets = list()
        self.txPackets = list()

        self.allPackets = list()

        self.allErrors = list()

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
            "allPackets": self.allPackets,
            "PErrors": self.allErrors
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

    def createPacketAndSave(self, line, lineNum):
        lineList = LineList(line)

        date = tryParseDate(line[0])
        typeP = findType(lineList)

        isSend = findIsSend(lineList)
        size = findSize(lineList)
        src = findSrc(lineList)
        dst = findDst(lineList)
        id = findId(lineList)

        if NULL in (date, typeP):
            printError(self.fileName, lineNum)
            return False

        packet = Packet(id, self.address, date, typeP,
                        src, dst, size - extraHeaderSize(typeP), size, str(isSend))

        if isDataPacket(typeP):
            packet.addVia(findVia(lineList))

        if isControlPacket(typeP):
            packet.addSeq_IdAndNum(findSeqId(lineList), findNum(lineList))

        self.addPacketToLists(packet, isSend)

        return True

    def createErrorAndSave(self, line):
        date = tryParseDate(line[0])

        pError = PError(self.address, date)
        self.allErrors.append(pError.__dict__)

    def getPackets(self):
        i = 0
        for index, line in enumerate(open(self.fileName, errors="ignore")):
            try:
                line = bytes(line, 'utf-8').decode('utf-8')

            except:
                continue

            listLine = line.split(" ")

            if 'Receiving LoRa packet' in line:
                self.allPacketsNum += 1
                continue

            if '> Packet ' in line:
                if self.createPacketAndSave(listLine, index):
                    i += 1

            if '> E:' in line:
                self.createErrorAndSave(listLine)


def printError(fileName, lineNum):
    print(F"Try parse failed at file {fileName}, line {lineNum}")


def tryParseDate(date) -> str:
    try:
        s = datetime.strptime(date, '%H:%M:%S.%f').strftime('%H:%M:%S.%f')
        return s[:-3]
    except:
        return NULL


def tryParseDateTime(date) -> datetime:
    try:
        s = datetime.strptime(date, '%H:%M:%S.%f')
        return s
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
    return (lineList.getIndexDefault("send") != -1)


def findId(lineList) -> int:
    try:
        return int(lineList.getNextValue("Id:"), 10)
    except:
        return NULL


def findType(lineList) -> int:
    try:
        return int(lineList.getNextValue("Type:"), 2)
    except:
        return NULL


def findSize(lineList) -> int:
    try:
        return int(lineList.getNextValue("Size:"), base=10)
    except:
        return NULL


def findDst(lineList) -> hex:
    try:
        return hex(int(lineList.getNextValue("Dst:"), base=16))
    except:
        return NULL


def findSrc(lineList) -> hex:
    try:
        return hex(int(lineList.getNextValue("Src:"), base=16))
    except:
        return NULL


def findVia(lineList) -> hex:
    try:
        return hex(int(lineList.getNextValue("Via:"), base=16))
    except:
        return NULL


def findSeqId(lineList) -> int:
    try:
        return int(lineList.getNextValue("Seq_Id:"), base=10)
    except:
        return NULL


def findNum(lineList) -> int:
    try:
        return int(lineList.getNextValue("Num:"), base=10)
    except:
        return NULL


class LineList(list):
    def getIndexDefault(self, elem):
        return self.index(elem) if elem in self else -1

    def getNextValue(self, elem):
        index = self.getIndexDefault(elem)
        if index == -1:
            return -1

        return self[index + 1] if index + 1 <= len(self) else -1


def identifySamePacketAndSetSameId(allPackets):
    for index, packet in enumerate(allPackets):
        for packetI in allPackets[index:]:
            if packet == packetI:
                packet["id"] = packet["id"]
                packetI["id"] = packet["id"]


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
        f"Number of bytes sent: {sizeOfAllSentInBytes}, Max possible Bytes received: {allBytesRx} bytes, Actual: {sizeOfAllReceivedInBytes} bytes. Loss: {lostBytes} bytes \n" +
        f"Total Loss: {(num_all_received / numAllPacketsRx - 1) * 100} %")


def saveCv():
    allPackets = []
    [allPackets.extend(node.allPackets) for node in nodes]

    # identifySamePacketAndSetSameId(allPackets)

    df = pd.DataFrame(allPackets)
    df.to_csv("data.csv")

# def saveWithoutDuplicates(data):
    # df = df.drop_duplicates(
    #     subset=['id', 'time', 'source', 'destination', 'isSend'], keep='first', inplace=False)


def showPlot():
    allPackets = []
    [allPackets.extend(node.allPackets) for node in nodes]

    for packet in allPackets:
        packet["time"] = tryParseDateTime(packet["time"])

    allPackets.sort(key=lambda x: x["time"], reverse=False)

    sendPackets = list(filter(lambda p: p["isSend"] == "True", allPackets))

    receivedPackets = list(
        filter(lambda p: p["isSend"] == "False", allPackets))

    for index, packet in enumerate(sendPackets):
        color = np.random.rand(3,)
        plt.scatter(packet["time"], index, color=color, s=100)
        previousTime = packet["time"]
        margin = 0
        numReceiveds = 0
        for packetI in receivedPackets:
            if packet["id"] == packetI["id"] and packet["source"] == packetI["source"]:
                maxTime = previousTime+timedelta(microseconds=100)
                if previousTime <= packetI["time"] or maxTime >= packetI["time"]:
                    margin += 1
                else:
                    previousTime = packetI["time"]
                    margin = 0

                plt.scatter(packetI["time"], index + margin * 0.5, color=color)
                plt.text(packetI["time"], index + margin *
                         0.5 + 0.1, packetI["deviceId"])

                numReceiveds += 1

        plt.text(packet["time"], index-0.7,
                 "SId: " + packet["deviceId"] + " NºR: " + str(numReceiveds) + " T: " + str(packet["type"]))

    allErrors = []
    [allErrors.extend(node.allErrors) for node in nodes]

    for pError in allErrors:
        pError["time"] = tryParseDateTime(pError["time"])

    allErrors.sort(key=lambda x: x["time"], reverse=False)

    if(len(allErrors) > 0):
        previousTime = allErrors[0]["time"]
        margin = 0
        for index, pError in enumerate(allErrors):
            maxTime = previousTime+timedelta(microseconds=200)
            if previousTime <= pError["time"] or maxTime >= pError["time"]:
                margin += 1
            else:
                previousTime = pError["time"]
                margin = 0
            plt.scatter(pError["time"], index +
                        margin * 0.5, color=(1.0, 0.0, 0.0))

            plt.text(pError["time"], index-0.7 + margin * 0.5,
                     "SId: " + pError["deviceId"])

    plt.gcf().autofmt_xdate()

    myFmt = mdates.DateFormatter('%H:%M:%S.%f')
    plt.gca().xaxis.set_major_formatter(myFmt)

    plt.show()


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

        saveCv()

        printGeneralInfo()

        showPlot()

        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        print('done')
