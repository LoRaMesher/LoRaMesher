from platformio import util
import os
import threading
import subprocess
from datetime import datetime
import atexit
import time

children = list()


def uploadToPort(portName, count):
    os.system("pio run --target upload --upload-port " + portName)
    print("Successfully update port: " + portName)

    time.sleep(count * 1)

    os.system(
        'cmd /c (pio device monitor --port ' + portName + ' --filter time) >> monitor_' + datetime.now().strftime("%H%M%S") + '_' + portName + '.txt')


def killThreads():
    for thread in children:
        thread.exit()

    raise SystemExit


if __name__ == '__main__':
    os.system("pio run")
    print("Successfully build")

    ports = util.get_serial_ports()
    for index, port in enumerate(ports):
        x = threading.Thread(target=uploadToPort, args=(port["port"], index,))
        children.append(x)
        x.start()

    atexit.register(killThreads)

    while(True):
        continue
