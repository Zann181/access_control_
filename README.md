# Access Control System 🚪🔒
### **Universidad Nacional de Colombia - Parcial Final de Estructuras Computacionales**
📌 **Presentado por:** Santiago Zambrano

---

## 📌 **Descripción del Proyecto**
Este es un **sistema de control de acceso** basado en el microcontrolador **STM32L476RG**, el cual permite la autenticación mediante una contraseña ingresada a través de un **teclado matricial**, y el control de una **puerta automatizada** mediante comandos por **UART** y un **botón físico**. 

El sistema utiliza un **display OLED** para mostrar información en tiempo real y **almacena los intentos de acceso**. Además, cuenta con un **modo de seguridad**, en el cual se bloquea tras varios intentos fallidos.

🎥 **Demostración en video:** [Ver en YouTube](https://youtu.be/DNegv7Jh04g)

---

## ⚙️ **Características principales**
✅ **Autenticación segura** con un teclado matricial.  
✅ **Control de acceso mediante comandos UART.**  
✅ **Interrupciones externas** para detección del botón y el timbre.  
✅ **Pantalla OLED SSD1306** para mostrar mensajes de estado.  
✅ **Estados de la puerta:**
   - 🔒 **Cerrada**
   - 🚪 **Abierta temporalmente**
   - 🔓 **Abierta permanentemente**
   - 🔔 **Modo timbre**
✅ **Tiempo de apagado automático** para evitar consumo innecesario de energía.  

---

## 🛠 **Componentes Utilizados**
- **Microcontrolador:** STM32L476RG (Nucleo Board)
- **Pantalla OLED:** SSD1306
- **Teclado matricial**
- **Botón físico para apertura manual**
- **Interfaz UART para control remoto**
- **LEDs indicadores de estado**
- **Timbre para señalizar eventos**
- **Fuente de alimentación 3.3V/5V**

---

## 🔧 **Configuración del Proyecto**
### **1️⃣ Configuración de Hardware**
| **Pin**  | **Función** | **Descripción** |
|----------|------------|----------------|
| `PA0`   | Botón      | Detección de apertura manual |
| `PA5`   | LED        | Indicador de estado |
| `PC11`  | Timbre     | Activación del timbre |
| `USART2` | UART       | Comunicación serie con la PC |
| `I2C1`  | I2C        | Comunicación con el OLED |

### **2️⃣ Configuración en STM32CubeMX**
- **GPIO:**
  - `PA0`: `External Interrupt Mode with Falling edge trigger detection`
  - `PC11`: `Output Mode` (para timbre)
  - `PA5`: `Output Mode` (para LED indicador)
- **UART:**
  - `USART2` habilitado para comunicación con la PC.
- **I2C:**
  - `I2C1` configurado para el **OLED SSD1306**.

---

## 📜 **Código Fuente Principal (`main.c`)**
El código principal realiza las siguientes tareas:
1. **Inicialización de periféricos** (UART, GPIO, I2C, OLED, Keypad).
2. **Manejo de la autenticación** mediante el teclado.
3. **Control de la puerta según comandos recibidos** (`*A*` para abrir, `*C*` para cerrar).
4. **Manejo de interrupciones**:
   - **Botón en PA0**: Abre la puerta cuando es presionado.
   - **Timbre en PC11**: Enciende una señal visual/auditiva.
5. **Sistema de seguridad**:
   - Se bloquea después de **tres intentos fallidos**.
   - Se apaga automáticamente tras **1 minuto de inactividad**.


