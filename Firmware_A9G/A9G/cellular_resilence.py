import cellular
import machine
import uasyncio
import socket
import time
from uasyncio import Event


async def resilence_tester(iterations,timeout,callback_test,commentError=None,comment=None,callback_reset=None):
    for i in range(iterations):
        try:
            if callback_test():
                print(comment)
                return True
            else:
                print(commentError)

        except Exception as e:
            print("Error:")
            print(e)

        if callback_reset is not None:
            callback_reset()
        await uasyncio.sleep(timeout)

    return False

def signal_quality():
    quality = cellular.get_signal_quality()
    print("Satations:  \n    {} \nSignal quality:\n    (QUALITY,RXQAL) = {},{} -> {}%,{}".format(cellular.stations(),quality[0],quality[1],quality[0]*100/31,quality[1]))
    return quality[0]*100/31

async def test_flight_mode():
    cellular.flight_mode(True)
    await uasyncio.sleep(10)
    cellular.flight_mode(False)
    await uasyncio.sleep(15)

async def connect_cellular_network():

    for i in range(10):

        #check SIM
        if not await resilence_tester(5, 1, cellular.is_sim_present,"Sim no detectada","SIM detectada"):
            print("Error (SIM no detectada):\n    reset A9G")
            machine.reset()
            continue

        #check signal quality
        if not await resilence_tester(10, 5, lambda: (signal_quality() >= 10) , "Calidad de señal inferior al 10%","Buena calidad de señal"):
            continue
        
        #check register network
        if not await resilence_tester(5, 5, cellular.is_network_registered, "No registrado en la red", "Registrado en la red"):
            await test_flight_mode()
            continue
        
        #check network status
        if not await resilence_tester(5, 10, lambda : (cellular.get_network_status() == 1),"Red inestable", "Red estable"):
            print("reset cellular network")
            cellular.reset()
            continue

        #check GPRS
        if not await resilence_tester(5, 3, lambda : cellular.gprs("nauta","",""),"Error GPRS","Conectado a internet",lambda : cellular.gprs(False)):
                print("reset cellular network")
                cellular.reset()
                print("init flight mode")
                test_flight_mode()
                print("end flight mode")
                continue
        else:
            return True

        # for i_gprs in range(5):
        #     try:
        #         if cellular.gprs("nauta","",""):
        #             print("Conectado a internet")
        #             return
        #         else:
        #             print("NO GPRS")

        #     except Exception as e:
        #         print("Error:")
        #         print(e)
            
        #     await uasyncio.sleep(3)

        # print("reset cellular network")
        # cellular.reset()
        # print("init flight mode")
        # test_flight_mode()
        # print("end flight mode")

    return False

async def ping(host):
    # Crea un socket TCP
    s = socket.socket()
    
    # Intenta conectarse al host en el puerto especificado
    try:
        s.connect((host, 1883))
        print("Port {} is open on {}".format(1883, host))
        s.close()
        return True
    except:
        print("Port {} is not open on {}".format(1883, host))
        return False


async def check_network_status(event,timeout,host):
    machine.watchdog_on(timeout*5)
    while True:
        print("Check network")
        if cellular.get_network_status() != 17 and not event.is_set():
            event.clear()
            if not await connect_cellular_network():
                continue
            event.set()
        print("Network OK")

        machine.watchdog_reset()
        await uasyncio.sleep(timeout)
    





