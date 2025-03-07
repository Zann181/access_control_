/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Cuerpo principal del programa para el sistema de control de acceso
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "keypad.h"
#include "ring_buffer.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "unlocked.h"
#include "locked.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/**
 * @brief Enumeración para los estados de la puerta.
 */
typedef enum {
    STATE_DOOR_CLOSED,          // Puerta cerrada
    STATE_DOOR_TEMP_OPEN,       // Puerta abierta temporalmente
    STATE_DOOR_PERM_OPEN,       // Puerta abierta permanentemente
    STATE_DOOR_RING              // Puerta en estado de timbre
} DoorState;

/**
 * @brief Enumeración para los eventos de la puerta.
 */
typedef enum {
    EVENT_OPEN_TEMP,            // Evento: abrir temporalmente
    EVENT_OPEN_PERM,            // Evento: abrir permanentemente
    EVENT_CLOSE,                // Evento: cerrar la puerta
    EVENT_RING                  // Evento: timbre
} DoorEvent;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define COMMAND_LENGTH 3
#define PASSWORD "1234" // Contraseña por defecto
#define MAX_PASSWORD_ATTEMPTS 3 // Máximo de intentos para la contraseña
#define DEBOUNCE_DELAY_MS 500 // Retardo de debounce para pulsaciones de botón
#define DOUBLE_PRESS_DELAY 1200 // Tiempo máximo entre dos pulsaciones rápidas
#define DOOR_TIMEOUT_MS 5000 // Tiempo para cerrar automáticamente la puerta
#define LED_ON_TIME 500 // Tiempo en milisegundos que el LED permanecerá encendido
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/**
 * @brief Enumeración para los estados del sistema.
 */
typedef enum {
    STATE_AUTHENTICATING,       // Esperando autenticación
    STATE_ACTIVE_DOOR_CLOSED,   // Autenticado, puerta cerrada
    STATE_OCCUPIED,             // Puerta abierta temporalmente
    STATE_UNOCCUPIED,           // Puerta abierta permanentemente
    STATE_LOCKED,               // Sistema bloqueado
    STATE_INACTIVE              // Sistema inactivo (tras 1 minuto)
} SystemState;
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

// Buffers circulares para diferentes entradas
ring_buffer_t pc_rx_buffer;      // Buffer para entrada del PC
uint8_t pc_rx_buffer_mem[128];

ring_buffer_t keypad_rx_buffer;  // Buffer para entrada del teclado
uint8_t keypad_rx_buffer_mem[128];

ring_buffer_t internet_rx_buffer; // Buffer para entrada de internet
uint8_t internet_rx_buffer_mem[128];

uint8_t rx_byte; // Variable para almacenar el byte recibido
uint32_t key_pressed_tick = 0; // Marca de tiempo para la pulsación de tecla
uint16_t column_pressed = 0; // Columna presionada en el teclado
uint32_t debounce_tick = 0; // Marca de tiempo para debounce

const char CMD_OPEN[] = "*A*"; // Comando para abrir la puerta
const char CMD_CLOSE[] = "*C*"; // Comando para cerrar la puerta
const char CMD_STATUS[] = "*1*"; // Comando para verificar el estado de la puerta

char current_cmd[COMMAND_LENGTH + 1]; // Buffer para el comando actual

uint8_t password_attempts = 0; // Contador de intentos de contraseña
uint8_t is_authenticated = 0; // Estado de autenticación

// Variables para el botón y el estado de la puerta
volatile uint8_t button_press_count = 0; // Contador de pulsaciones de botón
volatile uint8_t button_press_ring = 0; // Contador de pulsaciones del botón de timbre

uint32_t button_last_press_time = 0; // Marca de tiempo de la última pulsación de botón
uint32_t button_last_press_RING = 0; // Marca de tiempo de la última pulsación del botón de timbre

uint8_t door_state = 0; // 0: cerrada, 1: abierta temporalmente, 2: abierta permanentemente
uint32_t door_open_time = 0; // Marca de tiempo para el tiempo de apertura de la puerta

// Temporizador del sistema
uint32_t system_active_time = 0; // Tiempo en que el sistema se activa
#define SYSTEM_ACTIVE_DURATION 180000 // 1 minuto en milisegundos
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void uart_send_string(const char *str);
void display_menu(void);
void process_commands(void);
void handle_keypad_input(void);
void handle_authentication(void);
uint8_t check_password(const char *input);
void handle_button_press(void);
void reset_system(void); // Función para reiniciar el sistema
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief Callback de recepción completa de UART.
 * @param huart: Manejador de UART.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart2) {
        ring_buffer_write(&pc_rx_buffer, rx_byte); // Escribir el byte recibido en el buffer del PC
        HAL_UART_Transmit(&huart3, &rx_byte, 1, 100); // Eco del byte recibido a UART3
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1); // Rehabilitar la interrupción de recepción de UART para huart2
    }
    if (huart == &huart3) {
        ring_buffer_write(&internet_rx_buffer, rx_byte); // Escribir el byte recibido en el buffer de internet
        HAL_UART_Transmit(&huart2, &rx_byte, 1, 100); // Eco del byte recibido a UART2
        HAL_UART_Receive_IT(&huart3, &rx_byte, 1); // Rehabilitar la interrupción de recepción de UART para huart3
    }
}

/**
 * @brief Enviar una cadena a través de UART.
 * @param str: Puntero a la cadena a enviar.
 */
void uart_send_string(const char *str) {
    HAL_UART_Transmit(&huart2, (uint8_t *)str, strlen(str), 100); // Enviar cadena a UART2
    HAL_UART_Transmit(&huart3, (uint8_t *)str, strlen(str), 100); // Enviar cadena a UART3
}

/**
 * @brief Mostrar las opciones del menú en UART.
 */
void display_menu(void) {
    uart_send_string("\r\nMenu de opciones:\r\n");
    uart_send_string("Ingrese (*A*) para abrir la puerta\r\n");
    uart_send_string("Ingrese (*C*) para cerrar la puerta\r\n");
    uart_send_string("Ingrese (*1*) para ver el estado de la puerta\r\n");
}

/**
 * @brief Callback de interrupción externa para GPIO.
 * @param GPIO_Pin: Número del pin que activó la interrupción.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if ((HAL_GetTick() - debounce_tick) < DEBOUNCE_DELAY_MS) { // Verificación de debounce
        return; // Salir si está dentro del tiempo de debounce
    }
    debounce_tick = HAL_GetTick(); // Actualizar la marca de tiempo de debounce

    if (GPIO_Pin == B1_Pin) { // Verificar si se presionó el botón B1
        button_press_count++; // Incrementar el contador de pulsaciones de botón
        button_last_press_time = HAL_GetTick(); // Actualizar la última hora de pulsación
        uart_send_string("\r\nBotón B1 presionado\r\n"); // Registrar la pulsación del botón
    } else {
        // Manejar la entrada del teclado
        key_pressed_tick = HAL_GetTick(); // Actualizar la marca de tiempo de la pulsación de tecla
        column_pressed = GPIO_Pin; // Almacenar la columna presionada
        handle_keypad_input(); // Procesar la entrada del teclado
    } 
    if (GPIO_Pin == RING_Pin) { // Verificar si se presionó el botón de timbre
        HAL_GPIO_WritePin(LD_RING_GPIO_Port, LD_RING_Pin, GPIO_PIN_SET); // Encender el LED de timbre
        button_press_ring++; // Incrementar el contador de pulsaciones del botón de timbre
        button_last_press_RING = HAL_GetTick(); // Actualizar la última hora de pulsación
        button_press_ring = 0; // Reiniciar el contador de pulsaciones del botón de timbre
        // Verificar si el tiempo de encendido del LED ha expirado y apagarlo
        if (HAL_GetTick() - button_last_press_RING >= LED_ON_TIME) {
            HAL_GPIO_WritePin(LD_RING_GPIO_Port, LD_RING_Pin, GPIO_PIN_RESET); // Apagar el LED
        }
        uart_send_string("\r\nBotón RING presionado\r\n"); // Registrar la pulsación del botón de timbre
    }
}

/**
 * @brief Verificar si la contraseña ingresada coincide con la contraseña almacenada.
 * @param input: Puntero a la cadena de la contraseña ingresada.
 * @return 1 si la contraseña coincide, 0 en caso contrario.
 */
uint8_t check_password(const char *input) {
    return (strcmp(input, PASSWORD) == 0); // Comparar la entrada con la contraseña almacenada
}

/**
 * @brief Manejar el proceso de autenticación del usuario.
 */
void handle_authentication(void) {
    char password_input[5]; // Buffer para la entrada de la contraseña (4 dígitos + terminador nulo)
    uint8_t byte; // Variable para almacenar el byte recibido
    uint8_t idx = 0; // Índice para la entrada de la contraseña

    uart_send_string("Bienvenido a nuestro control de acceso\r\n");
    uart_send_string("Ingrese la contraseña para continuar: ");

    while (!is_authenticated) { // Bucle hasta que se autentique
        if (ring_buffer_read(&pc_rx_buffer, &byte) || ring_buffer_read(&internet_rx_buffer, &byte) || ring_buffer_read(&keypad_rx_buffer, &byte)) {
            password_input[idx++] = byte; // Almacenar el byte recibido en la entrada de la contraseña

            if (idx == 4) { // Verificar si se han ingresado 4 dígitos
                password_input[idx] = '\0'; // Terminar la cadena
                if (check_password(password_input)) { // Verificar si la contraseña es correcta
                    is_authenticated = 1; // Establecer la bandera de autenticación
                    system_active_time = HAL_GetTick(); // Activar el temporizador del sistema
                    uart_send_string("\r\nContraseña correcta. Acceso permitido.\r\n");

                    // Mostrar "Acceso permitido" en la pantalla OLED
                    ssd1306_Fill(Black);
                    ssd1306_SetCursor(10, 20);
                    ssd1306_WriteString("Acceso permitido", Font_7x10, White);
                    ssd1306_UpdateScreen();

                    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET); // Encender el LED
                    display_menu(); // Mostrar las opciones del menú
                } else {
                    password_attempts++; // Incrementar los intentos de contraseña
                    if (password_attempts >= MAX_PASSWORD_ATTEMPTS) { // Verificar si se alcanzó el máximo de intentos
                        uart_send_string("\r\n3 intentos fallidos. Sistema bloqueado.\r\n");
                        while (1); // Bloquear el sistema
                    } else {
                        uart_send_string("\r\nContraseña incorrecta. Intente de nuevo.\r\n");
                        uart_send_string("Ingrese la contraseña para continuar: ");
                    }
                    idx = 0; // Reiniciar el índice para el siguiente intento
                    ring_buffer_reset(&pc_rx_buffer); // Limpiar el buffer para evitar datos residuales
                }
            }
        }
    }
}

/**
 * @brief Reiniciar el sistema a su estado inicial.
 */
void reset_system(void) {
    is_authenticated = 0; // Reiniciar el estado de autenticación
    door_state = 0; // Establecer el estado de la puerta a cerrada
    HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET); // Asegurar que la puerta esté cerrada
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET); // Apagar el LED
    ssd1306_Fill(Black); // Limpiar la pantalla OLED
    ssd1306_UpdateScreen();
    uart_send_string("\r\nSistema reiniciado. \r\n"); // Registrar el reinicio del sistema
}

/**
 * @brief Manejar los eventos de pulsación de botón.
 */
void handle_button_press(void) {
    static uint32_t last_press_time = 0; // Marca de tiempo de la última pulsación de botón
    uint32_t current_time = HAL_GetTick(); // Obtener el tiempo actual

    // Verificar si es una pulsación doble
    if (button_press_count == 2 && (current_time - last_press_time) <= DOUBLE_PRESS_DELAY) {
        HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET); // Abrir la puerta
        ssd1306_Fill(Black);
        ssd1306_SetCursor(20, 20);
        ssd1306_WriteString("Door Open ", Font_6x8, White);
        ssd1306_SetCursor(20, 40);
        ssd1306_WriteString("PERMANENTLY", Font_6x8, White);
        ssd1306_UpdateScreen();
        uart_send_string("\r\nPuerta abierta permanentemente\r\n");
        door_state = 2; // Establecer el estado de la puerta a abierta permanentemente
        button_press_count = 0; // Reiniciar el contador de pulsaciones

    } else if (button_press_count == 1 && (current_time - last_press_time) > DOUBLE_PRESS_DELAY) {
        // Una pulsación: abrir temporalmente
        HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET); // Abrir la puerta
        ssd1306_Fill(Black);
        ssd1306_SetCursor(20, 20);
        ssd1306_WriteString("Door Open ", Font_6x8, White);
        ssd1306_SetCursor(20, 40);
        ssd1306_WriteString("TEMPORALITY ", Font_6x8, White);
        ssd1306_UpdateScreen();
        uart_send_string("\r\nPuerta abierta temporalmente\r\n");
        door_state = 1; // Establecer el estado de la puerta a abierta temporalmente
        door_open_time = HAL_GetTick(); // Registrar el tiempo de apertura
        button_press_count = 0; // Reiniciar el contador de pulsaciones
    } else if (button_press_ring == 1 && (current_time - last_press_time) > DOUBLE_PRESS_DELAY) {
        // Pulsación del botón de timbre
        door_state = STATE_DOOR_RING; // Establecer el estado de la puerta a timbre
        door_open_time = HAL_GetTick(); // Registrar el tiempo de apertura
        button_press_ring = 0; // Reiniciar el contador de pulsaciones del botón de timbre
    }

    // Actualizar la marca de tiempo de la última pulsación
    last_press_time = current_time;
}

/**
 * @brief Procesar los comandos recibidos a través de UART.
 */
void process_commands(void) {
    uint8_t byte; // Variable para almacenar el byte recibido
    while (ring_buffer_read(&pc_rx_buffer, &byte) || ring_buffer_read(&internet_rx_buffer, &byte) || ring_buffer_read(&keypad_rx_buffer, &byte)) {
        // Desplazar el buffer a la izquierda y añadir el nuevo byte
        memmove(current_cmd, current_cmd + 1, COMMAND_LENGTH - 1);
        current_cmd[COMMAND_LENGTH - 1] = (char)byte; // Añadir el nuevo byte al buffer de comandos
        current_cmd[COMMAND_LENGTH] = '\0'; // Asegurar que la cadena esté terminada

        // Comando *A*: Abrir puerta
        if (memcmp(current_cmd, CMD_OPEN, COMMAND_LENGTH) == 0) {
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET); // Abrir la puerta
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, unlocked_image_data, 128, 64, White); // Mostrar imagen de desbloqueo
            ssd1306_UpdateScreen();
            uart_send_string("\r\nComando *A* recibido: Puerta abierta\r\n");
        }
        // Comando *C*: Cerrar puerta
        else if (memcmp(current_cmd, CMD_CLOSE, COMMAND_LENGTH) == 0) {
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET); // Cerrar la puerta
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White); // Mostrar imagen de bloqueo
            ssd1306_UpdateScreen();
            uart_send_string("\r\nComando *C* recibido: Puerta cerrada\r\n");
        }
        // Comando *1*: Ver estado de la puerta
        else if (memcmp(current_cmd, CMD_STATUS, COMMAND_LENGTH) == 0) {
            uint8_t state = HAL_GPIO_ReadPin(DOOR_GPIO_Port, DOOR_Pin); // Leer el estado de la puerta
            uart_send_string(state ? "\r\nEstado de la puerta: Abierta\r\n" :
                                     "\r\nEstado de la puerta: Cerrada\r\n");
        }
    }
}

/**
 * @brief Manejar la entrada del teclado.
 */
void handle_keypad_input(void) {
    static uint32_t last_key_time = 0; // Último tiempo de tecla válida
    uint8_t key = keypad_scan(column_pressed); // Escanear el teclado para la tecla presionada

    if (key != 0) { // Verificar si se presionó una tecla
        uint32_t current_time = HAL_GetTick(); // Obtener el tiempo actual

        // Verificar si ha pasado el tiempo de debounce desde la última tecla
        if ((current_time - last_key_time) > DEBOUNCE_DELAY_MS) {
            last_key_time = current_time; // Actualizar el tiempo de la última tecla válida

            char key_str[2]; // Buffer para la cadena de la tecla
            key_str[0] = key; // Almacenar la tecla presionada
            key_str[1] = '\0'; // Terminar la cadena
            uart_send_string(key_str); // Enviar la tecla por UART
            ring_buffer_write(&keypad_rx_buffer, key); // Almacenar la tecla en el buffer
        }
    }
}

/**
 * @brief Máquina de estados para el sistema.
 */
void system_state_machine(void) {
    static uint32_t last_state_change = 0; // Marca de tiempo del último cambio de estado

    switch (door_state) {
        case STATE_DOOR_CLOSED:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET); // Asegurar que la puerta esté cerrada
            break;

        case STATE_DOOR_TEMP_OPEN:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET); // Abrir la puerta
            if (HAL_GetTick() - last_state_change > DOOR_TIMEOUT_MS) { // Verificar si ha ocurrido el tiempo de espera
                door_state = STATE_DOOR_CLOSED; // Establecer el estado de la puerta a cerrada
                last_state_change = HAL_GetTick(); // Actualizar la última hora de cambio de estado
                ssd1306_Fill(Black);
                ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White); // Mostrar imagen de bloqueo
                ssd1306_UpdateScreen();
                uart_send_string("\r\nPuerta cerrada después de 5 segundos\r\n");
            }
            break;

        case STATE_DOOR_PERM_OPEN:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET); // Mantener la puerta abierta
            break;
        
        case STATE_DOOR_RING:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, RING_Pin, GPIO_PIN_SET); // Activar el timbre
            break;

        default:
            break;
    }
}

/**
 * @brief Manejar eventos del sistema.
 * @param event: Evento a manejar.
 */
void system_events_handler(uint8_t event) {
    switch (event) {
        case EVENT_OPEN_TEMP:
            door_state = STATE_DOOR_TEMP_OPEN; // Establecer el estado de la puerta a abierta temporalmente
            door_open_time = HAL_GetTick(); // Registrar el tiempo de apertura
            ssd1306_Fill(Black);
            ssd1306_SetCursor(20, 20);
            ssd1306_WriteString("Door Open ", Font_6x8, White);
            ssd1306_SetCursor(20, 40);
            ssd1306_WriteString("TEMPORARILY", Font_6x8, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta abierta temporalmente\r\n");
            break;

        case EVENT_OPEN_PERM:
            door_state = STATE_DOOR_PERM_OPEN; // Establecer el estado de la puerta a abierta permanentemente
            ssd1306_Fill(Black);
            ssd1306_SetCursor(20, 20);
            ssd1306_WriteString("Door Open ", Font_6x8, White);
            ssd1306_SetCursor(20, 40);
            ssd1306_WriteString("PERMANENTLY", Font_6x8, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta abierta permanentemente\r\n");
            break;

        case EVENT_CLOSE:
            door_state = STATE_DOOR_CLOSED; // Establecer el estado de la puerta a cerrada
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White); // Mostrar imagen de bloqueo
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta cerrada\r\n");
            break;

        case EVENT_RING:
            door_state = STATE_DOOR_RING; // Establecer el estado de la puerta a timbre
            HAL_GPIO_WritePin(RING_GPIO_Port, RING_Pin, GPIO_PIN_RESET); // Reiniciar el pin de timbre
            uart_send_string("\r\ntimbre\r\n"); // Registrar el evento de timbre
            break;

        default:
            break;
    }
}
/* USER CODE END 0 */

/**
  * @brief  Punto de entrada de la aplicación.
  * @retval int
  */
int main(void) {
    HAL_Init(); // Inicializar la biblioteca HAL
    SystemClock_Config(); // Configurar el reloj del sistema
    MX_GPIO_Init(); // Inicializar GPIO
    MX_USART2_UART_Init(); // Inicializar UART2
    MX_I2C1_Init(); // Inicializar I2C1
    MX_USART3_UART_Init(); // Inicializar UART3

    // Inicializar buffers circulares
    ring_buffer_init(&pc_rx_buffer, pc_rx_buffer_mem, sizeof(pc_rx_buffer_mem));
    ring_buffer_init(&keypad_rx_buffer, keypad_rx_buffer_mem, sizeof(keypad_rx_buffer_mem));
    ring_buffer_init(&internet_rx_buffer, internet_rx_buffer_mem, sizeof(internet_rx_buffer_mem));

    // Iniciar interrupciones de recepción de UART
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);

    ssd1306_Init(); // Inicializar la pantalla OLED
    ssd1306_Fill(Black); // Limpiar la pantalla
    ssd1306_UpdateScreen(); // Actualizar la pantalla

    keypad_init(); // Inicializar el teclado

    while (1) {
        if (!is_authenticated) {
            handle_authentication(); // Solicitar la contraseña si no está autenticado
        } else {
            // Verificar si ha pasado 1 minuto desde que se activó el sistema
            if ((HAL_GetTick() - system_active_time) >= SYSTEM_ACTIVE_DURATION) {
                reset_system(); // Reiniciar el sistema después de 1 minuto
            }

            // Manejar pulsaciones del botón B1
            if (button_press_count > 0) {
                handle_button_press();
            }

            // Cerrar la puerta después de 5 segundos si está abierta temporalmente
            if (door_state == 1 && (HAL_GetTick() - door_open_time) >= 5000) {
                HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET); // Cerrar la puerta
                ssd1306_Fill(Black);
                ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White); // Mostrar imagen de bloqueo
                ssd1306_UpdateScreen();
                uart_send_string("\r\nPuerta cerrada después de 5 segundos\r\n");
                door_state = 0; // Establecer el estado de la puerta a cerrada
            }

            // Procesar comandos recibidos por UART
            process_commands();
        }
    }
}

/**
  * @brief Configuración del reloj del sistema.
  * @retval Ninguno
  */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configurar la salida del regulador interno principal
    */
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        Error_Handler(); // Manejar error
    }

    /** Inicializa los osciladores RCC según los parámetros especificados
    * en la estructura RCC_OscInitTypeDef.
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 10;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler(); // Manejar error
    }

    /** Inicializa la CPU, los buses AHB y APB
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler(); // Manejar error
    }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  Esta función se ejecuta en caso de que ocurra un error.
  * @retval Ninguno
  */
void Error_Handler(void) {
    /* USER CODE BEGIN Error_Handler_Debug */
    /* El usuario puede agregar su propia implementación para informar el estado de error de HAL */
    __disable_irq(); // Deshabilitar interrupciones
    while (1) {
        // Bucle infinito para el manejo de errores
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Informa el nombre del archivo fuente y el número de línea
  *         donde ocurrió el error assert_param.
  * @param  file: puntero al nombre del archivo fuente
  * @param  line: número de línea de error assert_param
  * @retval Ninguno
  */
void assert_failed(uint8_t *file, uint32_t line) {
    /* USER CODE BEGIN 6 */
    /* El usuario puede agregar su propia implementación para informar el nombre del archivo y el número de línea,
       por ejemplo: printf("Valor de parámetros incorrecto: archivo %s en línea %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */