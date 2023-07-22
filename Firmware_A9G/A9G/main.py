from uthingsboard.client import TBDeviceMqttClient
import cellular_resilence
import uasyncio
import ujson
import gps
from queue import Queue
from machine import UART

HOST = "iot-servercu.alascloud.com"
TOKEN = "pyuV6mMx0vMEigUNU0aK"
PORT = 1883
queue_rx = Queue()
queue_tx = Queue()
uart = UART(1,9600)


async def uart_tx():
    while True:
        data = await queue_tx.get()
        print(data)
        uart.write(data)

def uart_rx():
    buffer = ""
    data = ""
    receiving = False
    while True:
        await event_network_connected.wait()
        if uart.any():
            try:
                data = uart.read().decode()
                if not receiving and data.startswith('\n'):
                    data = data[1:]
                receiving = True
                buffer += data
            except:
                buffer = ""
                receiving = False

            if "\n" in data:
                try:
                    lines = buffer.split('\n')
                    for line in lines[:-1]:
                        print(line)
                        json_data = ujson.loads(line)
                        await queue_rx.put(json_data)
                    buffer = lines[-1]
                    receiving = False
                except:
                    print("Error: formato incorrecto")
                    # Enviando dato a la cola de datos recibidos
                    buffer = ""
                    receiving = False
        await uasyncio.sleep(0)

def msg_for_tx(key,value):
    print(ujson.dumps({key:value})+"\n")
    uart.write(ujson.dumps({key:value})+"\n")
    pass

def callback_RPC(req_id, payload):
    event_send_mqtt.clear()
    msg_for_tx("RPC",payload)
    TBClient.send_rpc_reply(req_id,payload)
    event_send_mqtt.set()

def callback_Atributes(data):
    event_send_mqtt.clear()
    msg_for_tx("Attributes",data)
    event_send_mqtt.set()


event_send_mqtt = uasyncio.Event()
TBClient = TBDeviceMqttClient(HOST,PORT,TOKEN,keepalive = 30)
TBClient.set_server_side_rpc_request_handler(callback_RPC)
TBClient.subscribe_to_all_attributes(callback_Atributes)
event_network_connected = uasyncio.Event()
event_location = uasyncio.Event()
tasks = []

async def send_location():
    while True:
        await event_location.wait()
        try:
            location = gps.get_location()
            TBClient.send_attributes({"latitude":location[0],"longitude":location[1]})
        except:
            gps.off()
            print("GPS ERROR")
            uart.write(ujson.dumps({"ERROR":"GPS"})+"\n")
        await uasyncio.sleep(10)

async def rx_processor():
    while True:
        obj =  await queue_rx.get()
        if obj.get("Attributes") is not None:
            TBClient.send_attributes(obj["Attributes"])
        elif obj.get("Telemetry") is not None:
            TBClient.send_telemetry(obj["Telemetry"])
        elif obj.get("RPC") is not None:
            TBClient.send_rpc_call(obj["RPC"]["method"],obj["RPC"]["params"],callback_RPC)
        elif obj.get("Location") is not None:
            if obj["Location"] == True:
                try:
                    gps.on()
                    print("GPS ON")
                    event_location.set()
                except:
                    gps.off()
                    print("GPS ERROR")
                    uart.write(ujson.dumps({"ERROR":"GPS"})+"\n")
            else:
                gps.off()
                print("GPS OFF")
        else:
            uart.write(ujson.dumps({"ERROR":"CODE"})+"\n")

        await uasyncio.sleep(0)

async def TBConnect():
    counter = 0
    while True:
        await event_network_connected.wait()
        if await TBClient.connect() != 0:
            print("TB no connected")
            counter += 1
            TBClient.reconnect()
            if counter >= 5 or not cellular_resilence.ping(HOST):
                counter = 0
                event_network_connected.clear()
                continue
        else:
            print("TB connected")
            TBClient.check_msg()
        await uasyncio.sleep(1)
    
    
async def main():
    event_send_mqtt.set()
    await uasyncio.sleep(5)
    uart.init(9600, bits=8, parity=None, stop=1)
    tasks.append(uasyncio.create_task(cellular_resilence.check_network_status(event_network_connected,10,HOST)))
    tasks.append(uasyncio.create_task(TBConnect()))
    tasks.append(uasyncio.create_task(uart_rx()))
    tasks.append(uasyncio.create_task(rx_processor()))
    tasks.append(uasyncio.create_task(send_location()))
    while True:
        await tasks

uasyncio.run(main())
