import uasyncio
from queue import Queue
from machine import UART
from machine import Pin

class Interface:
    def __init__(self):
        StateVariables = {
            "hardware": "A9G",                      # Hardware id
            "id": "0001",                           # Module id
            "fw_title": "GSM_Module",               # Firmware  id
            "fw_version": "0.0.1",                  # Firmware version
            "location": {                           # Real time location
                "latitude": None,                   
                "longitude": None        
            },
            "battery": None,                        # Battery state
            "mode": None                            # Master or Slave
        }
        TelemetrySend = {}
        TelemetryRe = {}
        RPCterminal = {
            "id": None,
            "Request": None,
            "Response": None
        }
        pass

class UartProtocol:
    def __init__(self, uart, queue_rx):
        self.uart = uart
        self.queue_rx = queue_rx
        self.queue_tx = queue_tx

    async def uart_reader(uart,queue_rx):
        buffer = ""
        while True:
            if uart.any():
                buffer += uart.read().decode()
                if "\n" in buffer:
                    data, buffer = buffer.split('\n',1)     # Recortando dato hasta el (\n)
                    await queue_rx.put(data)          # Enviando dato a la cola de datos recibidos