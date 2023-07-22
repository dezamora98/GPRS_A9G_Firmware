El objetivo de este repositorio es proporcionar una solución para la comunicación con la plataforma ThingsBoard, desplegada por AlaSoluciones en sus servidores, mediante el módulo A9G. La idea es aprovechar la capacidad de procesamiento del A9G([[a9g_product_specification.pdf]]) y las herramientas de software disponibles para liberar, en la medida de lo posible, al MCU (maestro) que utiliza al A9G como módem.

## Firmware A9G (Beta 1.0.0)

Tradicionalmente, se utilizaría el A9G con el firmware que brinda AiThinker, con comandos AT, pero esto implica tener que modificar las librerías de TinyGSM para agregar este módulo e implementar los mecanismos de resiliencia en el MCU maestro (normalmente ESP32), además de incorporar las librerías correspondientes para trabajar con ThingsBoard por MQTT o HTTP.

Para mitigar este problema, se implementó un firmware personalizado con MicroPython que utiliza unos comandos en formato JSON muy básicos por UART a 9600bps:

Enviar telemetría:

```json
	{"Telemetry": {<datos de telemetría>}}
```

Enviar atributos:

```json
	{"Attributes": {<datos de atributos>}}
```

Enviar RPC:

```json
	{"RPC": {<estructuras RPC>}}
```

Encender o apagar transmisión de localización GPS cada 10s:

```json
	{"Location": <True/False>}
```

La información desde el servidor es recibida mediante solicitudes RPC y atributos compartidos, y se envían por UART de manera asíncrona con una estructura similar a como se envían desde el MCU:

Recibir atributos compartidos:

```json
	{"Attributes": {<datos de atributos>}}
```

Recibir solicitudes RPC:

```json
	{"RPC": {<estructuras RPC>}}
```

En caso de existir algún tipo de error, el A9G notifica al MCU con las siguientes tramas:

Error de GPS:

```JSON
	{"ERROR":"GPS"}
```

Error de red:

```JSON
	{"ERROR":"Network"}
```

Error de comando inválido:

```JSON
	{"ERROR":"CODE"}
```

Con este firmware, al energizar el A9G, este intenta conectarse a la célula de mejor señal y, una vez conectado, intenta establecer comunicación con la plataforma. De ocurrir algún problema de comunicación, este se desconectará e intentará reconectarse durante 1 minuto cambiando a otras células, entrando en modo avión, apagando el servicio de GPRS y, de no poder reconectarse, se reiniciará completamente y en 5s intentará conectarse nuevamente.

## Librería TBObj (Beta 1.0.0)
Para abstraer aún más al desarrollador, se implementó una librería llamada TBObj.h (depende de ArduinoJson y FreeRTOS) que se utiliza para generar las tramas de envío y recepción de datos con ThingsBoard de manera automática y transparente. Esto se logra creando un contenedor de objetos que serán enviados a la plataforma o actualizados desde esta. Este contenedor implementa dos tareas concurrentes que pueden ser manipuladas mediante funciones callback para implementar la recepción y transmisión de datos:

```C++
using SendCallback = std::function<void(String)>;
TBObjContainer(uint64_t minTimeOut = 2500, SendCallback s_callback = defaultSendCallback, TaskFunction_t rx_callback = defaultRxCallback)
```

Para enviar un objeto a la plataforma, tienen que cumplirse tres condiciones:
- El objeto es nuevo o ha cambiado desde la última vez que se envió.
- El tiempo mínimo definido para enviar el objeto ha transcurrido.
- El tiempo mínimo para enviar cualquier objeto ha transcurrido (_minTimeOut_)

Nótese que las funciones callback de envío y recepción de datos se encuentran predefinidas, al igual que el parámetro _minTimeOut_. Estas funciones ya están probadas con el protocolo implementado en el A9G y el tiempo mínimo está calculado con un margen de tolerancia para que se consuman 500 MB en un mes de transmisión sin interrupción alguna.

El contenedor realmente almacena punteros a una definición base llamada _TBObj_base_, del cual hereda un objeto plantilla _TBObj<>_.

```C++
TBObj(String name, T data, TBObjContainer &TBObjects, uint8_t mode = M_TELEMETRY, uint8_t inOut = IO_OUT, u_int64_t timeOut = 5000)
```

Nótese que este objeto presenta varios parámetros para definir su comportamiento y puede ser engorroso o confuso crearlos; por tanto, se definieron las siguientes macros:

```C++
	Telemetry_OUT(type, name, TimeOut);
	SharedAttribute(type, name, TimeOut);
	Attribute_IN(type, name);
	Attribute_OUT(type, name, TimeOut);
	RPCData_OUT(type, name, TimeOut);
	RPCData_IN(type, name);
	RPCFunction_IN(callback);
```

Estas macros permiten definir el tipo de dato, el nombre del objeto y el tiempo de actualización de manera automática. El nombre de la variable y el nombre del objeto son los mismos para crear una homogeneidad entre las variables codificadas y los datos transmitidos a la plataforma.

### Ejemplo 1:
```C++
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "TBObj.h"

void sendCommand(StaticJsonDocument<256>& doc)
{
    String strOut;
    serializeJson(doc,strOut);
    debugln(strOut.c_str());
}

void setup()
{
    randomSeed(millis());
}

void loop()
{
    Telemetry_OUT(int,algo,10000);
    SharedAttribute(String,algo2,5000);
    RPCData_IN(bool,TEST);
    RPCFunction_IN(sendCommand);

    while (true)
    {
        algo = random(100);
        algo2 = static_cast<String>(random(100));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        debugln(algo2._Data.c_str());
        if(TEST == true)
        {
            debugln("la variable TEST cambió");
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
```

Este ejemplo muestra cómo crear variables para telemetría, atributos compartidos, datos que cambian por RPC de entrada y funciones RPC. Note que en el campo del nombre de la variable se define la misma variable y puede seguir trabajando con este nombre.

## Deficiencias Firmware A9G:

### Problemas con el GPS:

En la versión Beta 1.0.0 desarrollada en MicroPython, si bien es funcional y fácil de mantener debido a las bondades de este lenguaje, tiene el inconveniente de que es necesario sacrificar la interfaz UART-1 del A9G para poder acceder al intérprete. Esto provoca que solo se pueda utilizar la interfaz UART-2 para el protocolo de comunicación con el maestro (ESP32).

La interfaz UART-2 dentro del módulo A9G conecta al MCU principal ([RDA8955](doc/RDA8955L_Datasheet_v1.0.0.pdf)) con el módulo GPS ([GK9501](doc/gk9501_inoutformat_20190716.pdf)). Internamente, el UART-2 se utiliza para reprogramar el GK9501, configurarlo y realizar peticiones de localización. Por tanto, aunque en la API del OS de Ai-Thinker y en el port de MicroPython para A9/A9G sea posible utilizar esta interfaz UART sin restricciones, en el caso del módulo A9G no es recomendado ya que interfiere con la comunicación con el GK9501. Lo anterior indica que en esta primera versión del firmware no es posible utilizar el módulo GPS.

### Problemas de seguridad:

Como MicroPython utiliza una configuración estándar para su terminal y todas las opciones que son accesibles desde esta, resulta muy fácil extraer el firmware y realizarle ingeniería inversa al código. Esto pudiera provocar que terceros clonen los dispositivos o realicen accesos no autorizados a la plataforma.

#### Problemas de seguridad asociados al protocolo:

Otra de las deficiencias de seguridad encontradas es que, en esta versión beta, el protocolo entre el maestro (ESP32) y el esclavo (A9G) se realiza mediante cadenas de caracteres con estructura JSON, lo cual las hace muy intuitivas y fáciles de identificar y clonar. Por otro lado, este protocolo no tiene ningún mecanismo para la detección y corrección de errores además de la detección de problemas de formato en las tramas. Esto último no es del todo necesario debido a que los requerimientos de la implementación no son tan críticos y pueden ser tolerados estos errores. No obstante, se está trabajando en una implementación más robusta del protocolo.

### Plan de Actualizaciones:

- **Beta 1.0.1**: Migrar todo el firmware a C utilizando la API y el OS de Ai-Thinker para corregir los problemas con el GPS y de seguridad que traen consigo el uso de MicroPython. Aunque no se debe descartar esta implementación para proyectos simples, portables y que requieran un tiempo de desarrollo mínimo.
- **Beta 1.0.2**: Actualización del protocolo de comunicación y añadir tramas tipo “get” para obtener parámetros de estado del módulo de comunicación y tipo “set” para configurarlos.
- **Beta 1.0.3**: Agregar un mecanismo de detección y corrección de errores en el protocolo.
- **Beta 1.0.4**: Añadir la capa de seguridad SSL para las transacciones MQTT con la plataforma.
- **1.0.5**: Corregir todas las deficiencias detectadas hasta la versión Beta 1.0.4.
- **1.0.6**: Se espera que esta sea la versión estable y final del firmware.

## Deficiencias TBObj:

Por ahora, la mayor deficiencia de esta librería es el uso de la librería Arduino JSON, la cual reclama un alto uso memoria de programa y de datos del microcontrolador; por tanto, no es la mejor opción para su uso en microcontroladores con capacidades reducidas.

Otro factor a tener en cuenta es que los TBObj solo sobrecargan el operador “=” para poder tener una compatibilidad coherente con todos los tipos serializables. Por tanto, si el desarrollador desea utilizar otros operadores aritméticos/lógicos debe acceder al objeto encapsulado dentro del TBObj con el método **getData** o accediendo directamente a este objeto con el miembro público **Data**.

### Plan de Actualizaciones:

- **Beta 1.0.1**: Sustituir la librería **ArduinoJSON** por [TinyJson](https://github.com/rafagafe/tiny-json) con una posible reducción del uso de recursos de memoria en un 60% o más.
- **Beta 1.0.2**: Mejorar la forma de crear los TBObj.
* Esta librería está propensa a actualizaciones a partir de la versión beta 1.0.2 del firmware del A9G. una vez estandarizado el protocolo con el esclavo no tendrá cambios mayores. 