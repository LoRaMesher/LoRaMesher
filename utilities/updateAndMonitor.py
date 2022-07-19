from platformio import util
import os
import threading
import subprocess
from datetime import datetime
import atexit
import time
import random

children = list()

portsTtgoTBeam = {"COM18", "COM19", "COM20"}


def uploadToPort(portName, count):
    env = "ttgo-t-beam"
    if portName in portsTtgoTBeam:
        env = "ttgo-lora32-v1"

    os.system("pio run -e " + env +
              " --target upload --upload-port " + portName)
    print("Successfully update port: " + portName)

    os.system(
        'cmd /c (pio device monitor --port ' + portName + ' -f esp32_exception_decoder -f time) >> monitor_' + datetime.now().strftime("%H%M%S") + '_' + portName + '.txt')


def killThreads():
    for thread in children:
        thread.exit()

    raise SystemExit


if __name__ == '__main__':
    os.system("pio run")
    print("Successfully build")

    ports = util.get_serial_ports()
    for index, port in enumerate(ports):
        x = threading.Thread(target=uploadToPort,
                             args=(port["port"], index,))
        children.append(x)
        x.start()

    atexit.register(killThreads)

    while(True):
        continue
