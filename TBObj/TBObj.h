#ifndef TBOBJ_H
#define TBOBJ_H

#include <iostream>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <memory>
#include <vector>
#include <type_traits>

#define DEBUG
#ifdef DEBUG
#define debugln(msg) printf((static_cast<String>(msg) + "\n").c_str())
#else
#define debugln(msg)
#endif

typedef enum
{
    M_TELEMETRY = 0,
    M_ATTRIBUTE,
    M_RPC

} TB_Modes;

typedef enum
{
    IO_OUT = 0,
    IO_IN,
    IO_IO,
} TB_IO;

class TBObj_base
{
public:
    TBObj_base(String name, uint8_t mode = M_TELEMETRY, uint8_t inOut = IO_OUT, u_int64_t timeOut = 1000)
    {
        _Name = name;
        _Mode = mode;
        _InOut = inOut;
        _Changed = true;
        _TimeOut = timeOut;
        _LastSendTime = xTaskGetTickCount();
        debugln("- objeto (" + name + ") creado");
    }
    virtual ~TBObj_base() {}
    virtual bool isChanged() const { return _Changed; }
    virtual String getJsonDocData() { return ""; }
    virtual void setData(StaticJsonDocument<256> &doc) {}
    virtual void setChanged() { _Changed = true; }
    TickType_t _LastSendTime;
    String _Name;
    uint8_t _Mode;
    uint8_t _InOut;
    u_int64_t _TimeOut;
    bool _Changed;
};

class TBObjContainer
{
public:
    using SendCallback = std::function<void(String)>;
    TBObjContainer(uint64_t minTimeOut = 2500, SendCallback s_callback = defaultSendCallback, TaskFunction_t rx_callback = defaultRxCallback)
    {
        // debugln("- creando contenedor");
        xTaskCreate(rx_callback, "rx_callback", 1024 * 3, this, configMAX_PRIORITIES, NULL);
        xTaskCreate(checkTask, "checkTask", 2048, this, 5, NULL);
        _Callback = s_callback;
        _MinTimeOut = minTimeOut;
    }

    ~TBObjContainer()
    {
        for (auto obj : _Container_Out)
        {
            delete obj;
        }
        for (auto obj : _Container_In)
        {
            delete obj;
        }
    }

    void add(TBObj_base *data)
    {
        switch (data->_InOut)
        {
        case IO_OUT:
            _Container_Out.push_back(data);
            break;
        case IO_IN:
            _Container_In.push_back(data);
            break;
        case IO_IO:
            _Container_In.push_back(data);
            _Container_Out.push_back(data);
            break;
        default:
            debugln(("ERROR in definition IO mode for (" + data->_Name + ")").c_str());
            break;
        }
    }

    uint64_t _MinTimeOut;
    SendCallback _Callback;
    std::vector<TBObj_base *> _Container_Out;
    std::vector<TBObj_base *> _Container_In;

private:
    static void checkTask(void *pvParameter);
    static void defaultRxCallback(void *pvParameter);
    static void defaultSendCallback(String msg);
};

inline void processor_rx(std::vector<TBObj_base *> &container, StaticJsonDocument<256> &doc, String &StDoc)
{
    uint8_t mode;
    String key;
    if (doc.containsKey("Attributes"))
    {
        mode = M_ATTRIBUTE;
        key = "Attributes";
        if (doc["Attributes"].containsKey("deleted"))
        {
            // implementar el algoritmo para eliminar;
            /*
                JsonArray array = doc[key]["deleted"];
                for (const char *value : array)
                {
                    container.erase(std::remove_if(
                                        container.begin(), container.end(), [value](const TBObj_base *obj)
                                        { return obj->_Name == value; }),
                                    container.end());
                }
                serializeJson(doc, Serial1);
            */
            return;
        }
    }
    else if (doc.containsKey("RPC"))
    {
        mode = M_RPC;
        key = "RPC";
    }
    else
    {
        debugln(StDoc.c_str());
        return;
    }

    for (auto it = container.begin(); it != container.end();)
    {
        if (*it == nullptr)
        {
            debugln("puntero nulo eliminado");
            it = container.erase(it);
        }
        else
        {
            TBObj_base *obj = *it;
            if (obj->_InOut != IO_OUT && obj->_Mode == mode)
            {
                debugln(obj->_Name.c_str());

                if (obj->_Name == doc[key]["method"] || doc[key].containsKey(obj->_Name))
                {
                    obj->setData(doc);
                }
            }
            ++it;
        }
    }
}

void TBObjContainer::defaultRxCallback(void *pvParameter)
{
    Serial1.begin(9600, SERIAL_8N1, 16, 17);
    String buffer;
    while (true)
    {
        if (Serial1.available())
        {
            char data = Serial1.read();
            buffer += data;
            if (data == '\n')
            {
                StaticJsonDocument<256> doc;
                DeserializationError error = deserializeJson(doc, buffer.c_str());
                if (!error)
                {
                    processor_rx(((TBObjContainer *)pvParameter)->_Container_In, doc, buffer);
                }
                else
                {
                    // Manejar el caso en que la cadena no sea un JSON válido
                }
                buffer.clear();
            }
        }
        vTaskDelay(1);
    }
}

void TBObjContainer::defaultSendCallback(String msg)
{
    debugln(msg.c_str());
    Serial1.println(msg);
}

void TBObjContainer::checkTask(void *pvParameter)
{
    TBObjContainer *args = static_cast<TBObjContainer *>(pvParameter);
    while (true)
    {
        TickType_t current_time = xTaskGetTickCount();
        for (auto it = args->_Container_Out.begin(); it != args->_Container_Out.end();)
        {
            if (*it == nullptr)
            {
                it = args->_Container_Out.erase(it);
            }
            else
            {
                TBObj_base *obj = *it;
                if ((current_time - obj->_LastSendTime) >= obj->_TimeOut && obj->_Changed && obj->_InOut != IO_IN)
                {
                    args->_Callback(obj->getJsonDocData());
                }
                ++it;
            }
        }
        vTaskDelay(args->_MinTimeOut / portTICK_PERIOD_MS);
    }
}

template <typename T>
class TBObj : public TBObj_base
{
public:
    TBObj(String name, T data, TBObjContainer &TBObjects, uint8_t mode = M_TELEMETRY, uint8_t inOut = IO_OUT, u_int64_t timeOut = 5000) : TBObj_base(name, mode, inOut, timeOut)
    {
        _Data = data;
        TBObjects.add(this);
    }

    String getJsonDocData()
    {
        StaticJsonDocument<200> doc;
        switch (_Mode)
        {
        case M_TELEMETRY:
            // debugln("- asignando name y data");
            doc["Telemetry"][_Name] = _Data;
            // debugln("- asignado");
            break;
        case M_ATTRIBUTE:
            doc["Attributes"][_Name] = _Data;
            break;
        case M_RPC:
            doc["RPC"]["method"] = _Name;
            doc["RPC"]["params"]["setValue"] = _Data;
            break;
        default:
            break;
        }
        // debugln("- actualizando tiempo");
        _LastSendTime = xTaskGetTickCount();
        _Changed = false;
        // debugln("- saliendo");
        String strOut;
        serializeJson(doc, strOut);
        return strOut;
    }
    T operator=(const T &other)
    {
        _Data = other;
        _Changed = true;
        // debugln("- objeto (" + _Name + ") modificado");
        return _Data;
    }
    bool operator==(const T &other) const
    {
        return _Data == other;
    }
    bool operator!=(const T &other) const
    {
        return _Data != other;
    }
    bool operator>=(const T &other) const
    {
        return _Data >= other;
    }
    bool operator<=(const T &other) const
    {
        return _Data <= other;
    }
    bool operator>(const T &other) const
    {
        return _Data > other;
    }
    bool operator<(const T &other) const
    {
        return _Data < other;
    }

    void setData(StaticJsonDocument<256> &doc)
    {
        if (_Mode == M_ATTRIBUTE)
        {
            _Data = doc["Attributes"][_Name].as<T>();
        }
        else if (_Mode == M_RPC)
        {
            _Data = doc["RPC"]["params"].as<T>();
        }
    }
    T getData()
    {
        return _Data;
    }

    T _Data;
};

class RPC_FN_IN : TBObj_base
{
public:
    using Callback = std::function<void(StaticJsonDocument<256> &)>;

    RPC_FN_IN(String name, Callback function, TBObjContainer &TBObjects) : TBObj_base(name, M_RPC, IO_IN)
    {
        _Function = function;
        TBObjects.add(this);
    }

    void setData(StaticJsonDocument<256> &doc)
    {
        _Function(doc);
    }

private:
    Callback _Function;
};

TBObjContainer TBObjects;

void GPS_Location(bool state)
{
    StaticJsonDocument<20> send;
    send["Location"] = state;
    String strOut;
    serializeJson(send,strOut);
    Serial1.println(strOut);
}

#define Telemetry_OUT(type, name, TimeOut) TBObj<type> name(#name, type(), TBObjects, M_TELEMETRY, IO_OUT, TimeOut)
#define SharedAttribute(type, name, TimeOut) TBObj<type> name(#name, type(), TBObjects, M_ATTRIBUTE, IO_IO, TimeOut)
#define Attribute_IN(type, name) TBObj<type> name(#name, type(), TBObjects, M_ATTRIBUTE, IO_IN)
#define Attribute_OUT(type, name, TimeOut) TBObj<type> name(#name, type(), TBObjects, M_ATTRIBUTE, IO_OUT, TimeOut)
#define RPCData_OUT(type, name, TimeOut) TBObj<type> name(#name, type(), TBObjects, M_RPC, IO_OUT, TimeOut)
#define RPCData_IN(type, name) TBObj<type> name(#name, type(), TBObjects, M_RPC, IO_IN)
#define RPCFunction_IN(callback) RPC_FN_IN name(#callback, callback, TBObjects)

#endif // !TBOBJ_H