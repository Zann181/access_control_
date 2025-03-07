/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
typedef enum {
    STATE_DOOR_CLOSED,          // Puerta cerrada
    STATE_DOOR_TEMP_OPEN,       // Puerta abierta temporalmente
    STATE_DOOR_PERM_OPEN        // Puerta abierta permanentemente
} DoorState;

typedef enum {
    EVENT_OPEN_TEMP,            // Evento: abrir temporalmente
    EVENT_OPEN_PERM,            // Evento: abrir permanentemente
    EVENT_CLOSE                 // Evento: cerrar puerta
} DoorEvent;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define COMMAND_LENGTH 3
#define PASSWORD "1234"
#define MAX_PASSWORD_ATTEMPTS 3
#define DEBOUNCE_DELAY_MS 500
#define DOUBLE_PRESS_DELAY 1200 // Tiempo máximo entre dos pulsaciones rápidas
#define DOOR_TIMEOUT_MS 5000    // Tiempo para 
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE BEGIN PTD */
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
/* USER CODE BEGIN PV */
uint8_t rx_byte;
uint32_t key_pressed_tick = 0;
uint16_t column_pressed = 0;
uint32_t debounce_tick = 0;

const char CMD_OPEN[] = "*A*";
const char CMD_CLOSE[] = "*C*";
const char CMD_STATUS[] = "*1*";


ring_buffer_t rx_buffer;
uint8_t rx_buffer_mem[128];
char current_cmd[COMMAND_LENGTH + 1];

uint8_t password_attempts = 0;
uint8_t is_authenticated = 0;

// Variables para el botón y la puerta
volatile uint8_t button_press_count = 0;
uint32_t button_last_press_time = 0;
uint8_t door_state = 0; // 0: cerrada, 1: abierta temporalmente, 2: abierta permanentemente
uint32_t door_open_time = 0;

// Temporizador del sistema
uint32_t system_active_time = 0; // Tiempo en que el sistema se activa
#define SYSTEM_ACTIVE_DURATION 60000 // 1 minuto en milisegundos
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
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart2) {
        ring_buffer_write(&rx_buffer, rx_byte);
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    }
    if (huart == &huart3) {
        HAL_UART_Transmit(&huart2, &rx_byte, 1, 100);
        HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    }
}

void uart_send_string(const char *str) {
    HAL_UART_Transmit(&huart2, (uint8_t *)str, strlen(str), 100);
}

void display_menu(void) {
    uart_send_string("\r\nMenu de opciones:\r\n");
    uart_send_string("Ingrese (*A*) para abrir la puerta\r\n");
    uart_send_string("Ingrese (*C*) para cerrar la puerta\r\n");
    uart_send_string("Ingrese (*1*) para ver el estado de la puerta\r\n");
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if ((HAL_GetTick() - debounce_tick) < DEBOUNCE_DELAY_MS) { // Debounce
        return;
    }
    debounce_tick = HAL_GetTick();

    if (GPIO_Pin == B1_Pin) { // Verificar si el botón B1 fue presionado
        button_press_count++;
        button_last_press_time = HAL_GetTick();
        uart_send_string("\r\nBotón B1 presionado\r\n");
    } else {
        // Manejar teclado
        key_pressed_tick = HAL_GetTick();
        column_pressed = GPIO_Pin;
        handle_keypad_input();
    }
}

uint8_t check_password(const char *input) {
    return (strcmp(input, PASSWORD) == 0);
}

void handle_authentication(void) {
    char password_input[5]; // 4 dígitos + 1 para el carácter nulo
    uint8_t byte;
    uint8_t idx = 0;

    uart_send_string("Bienvenido a nuestro control de acceso\r\n");
    uart_send_string("Ingrese la contraseña para continuar: ");

    while (!is_authenticated) {
        if (ring_buffer_read(&rx_buffer, &byte)) {
            password_input[idx++] = byte;

            if (idx == 4) { // La contraseña tiene 4 dígitos
                password_input[idx] = '\0'; // Terminar la cadena
                if (check_password(password_input)) {
                    is_authenticated = 1;
                    system_active_time = HAL_GetTick(); // Activar el temporizador del sistema
                    uart_send_string("\r\nContraseña correcta. Acceso permitido.\r\n");

                    // Mostrar "Acceso permitido" en la pantalla OLED
                    ssd1306_Fill(Black);
                    ssd1306_SetCursor(10, 20);
                    ssd1306_WriteString("Acceso permitido", Font_7x10, White);
                    ssd1306_UpdateScreen();

                    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET); // Encender el LED
                    display_menu();
                } else {
                    password_attempts++;
                    if (password_attempts >= MAX_PASSWORD_ATTEMPTS) {
                        uart_send_string("\r\n3 intentos fallidos. Sistema bloqueado.\r\n");
                        while (1); // Bloquear el sistema
                    } else {
                        uart_send_string("\r\nContraseña incorrecta. Intente de nuevo.\r\n");
                        uart_send_string("Ingrese la contraseña para continuar: ");
                    }
                    idx = 0; // Reiniciar el índice para el siguiente intento
                    ring_buffer_reset(&rx_buffer); // Limpiar el buffer para evitar datos residuales
                }
            }
        }
    }
}

void reset_system(void) {
    is_authenticated = 0; // Reiniciar autenticación
    door_state = 0; // Cerrar la puerta
    HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET); // Asegurar que la puerta esté cerrada
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET); // Apagar el LED
    ssd1306_Fill(Black); // Limpiar la pantalla OLED
    ssd1306_UpdateScreen();
    uart_send_string("\r\nSistema reiniciado. \r\n");
}

void handle_button_press(void) {
    static uint32_t last_press_time = 0;
    uint32_t current_time = HAL_GetTick();

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
        door_state = 2; // Estado de puerta abierta permanentemente
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
        door_state = 1; // Estado de puerta abierta temporalmente
        door_open_time = HAL_GetTick(); // Registrar el tiempo de apertura
        button_press_count = 0; // Reiniciar el contador de pulsaciones
    }

    // Actualizar el tiempo de la última pulsación
    last_press_time = current_time;
}

void process_commands(void) {
    uint8_t byte;
    while (ring_buffer_read(&rx_buffer, &byte)) {
        // Shift buffer left y añade el nuevo byte
        memmove(current_cmd, current_cmd + 1, COMMAND_LENGTH - 1);
        current_cmd[COMMAND_LENGTH - 1] = (char)byte;
        current_cmd[COMMAND_LENGTH] = '\0'; // Asegurar que la cadena esté terminada

        // Comando *A*: Puerta abierta
        if (memcmp(current_cmd, CMD_OPEN, COMMAND_LENGTH) == 0) {
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET);
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, unlocked_image_data, 128, 64, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nComando *A* recibido: Puerta abierta\r\n");
        }
        // Comando *C*: Puerta cerrada
        else if (memcmp(current_cmd, CMD_CLOSE, COMMAND_LENGTH) == 0) {
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET);
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nComando *C* recibido: Puerta cerrada\r\n");
        }
        // Comando *1*: Estado de la puerta
        else if (memcmp(current_cmd, CMD_STATUS, COMMAND_LENGTH) == 0) {
            uint8_t state = HAL_GPIO_ReadPin(DOOR_GPIO_Port, DOOR_Pin);
            uart_send_string(state ? "\r\nEstado de la puerta: Abierta\r\n" :
                                     "\r\nEstado de la puerta: Cerrada\r\n");
        }

    }
}

void handle_keypad_input(void) {
    static uint32_t last_key_time = 0; // Tiempo de la última tecla válida
    uint8_t key = keypad_scan(column_pressed);

    if (key != 0) {
        uint32_t current_time = HAL_GetTick();

        // Verificar si ha pasado el tiempo de debounce desde la última tecla
        if ((current_time - last_key_time) > DEBOUNCE_DELAY_MS) {
            last_key_time = current_time; // Actualizar el tiempo de la última tecla válida

            char key_str[2];
            key_str[0] = key;
            key_str[1] = '\0';
            uart_send_string(key_str); // Enviar la tecla por UART
            ring_buffer_write(&rx_buffer, key); // Almacenar la tecla en el buffer
        }
    }
}

void system_state_machine(void) {
    static uint32_t last_state_change = 0;

    switch (door_state) {
        case STATE_DOOR_CLOSED:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_RESET);
            break;

        case STATE_DOOR_TEMP_OPEN:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET);
            if (HAL_GetTick() - last_state_change > DOOR_TIMEOUT_MS) {
                door_state = STATE_DOOR_CLOSED;
                last_state_change = HAL_GetTick();
                ssd1306_Fill(Black);
                ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White);
                ssd1306_UpdateScreen();
                uart_send_string("\r\nPuerta cerrada después de 5 segundos\r\n");
            }
            break;

        case STATE_DOOR_PERM_OPEN:
            HAL_GPIO_WritePin(DOOR_GPIO_Port, DOOR_Pin, GPIO_PIN_SET);
            break;

        default:
            break;
    }
}

void system_events_handler(uint8_t event) {
    switch (event) {
        case EVENT_OPEN_TEMP:
            door_state = STATE_DOOR_TEMP_OPEN;
            door_open_time = HAL_GetTick();
            ssd1306_Fill(Black);
            ssd1306_SetCursor(20, 20);
            ssd1306_WriteString("Door Open ", Font_6x8, White);
            ssd1306_SetCursor(20, 40);
            ssd1306_WriteString("TEMPORARILY", Font_6x8, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta abierta temporalmente\r\n");
            break;

        case EVENT_OPEN_PERM:
            door_state = STATE_DOOR_PERM_OPEN;
            ssd1306_Fill(Black);
            ssd1306_SetCursor(20, 20);
            ssd1306_WriteString("Door Open ", Font_6x8, White);
            ssd1306_SetCursor(20, 40);
            ssd1306_WriteString("PERMANENTLY", Font_6x8, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta abierta permanentemente\r\n");
            break;

        case EVENT_CLOSE:
            door_state = STATE_DOOR_CLOSED;
            ssd1306_Fill(Black);
            ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White);
            ssd1306_UpdateScreen();
            uart_send_string("\r\nPuerta cerrada\r\n");
            break;

        default:
            break;
    }
}
/* USER CODE END 0 */

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();
    MX_USART3_UART_Init();

    ring_buffer_init(&rx_buffer, rx_buffer_mem, sizeof(rx_buffer_mem));
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

    ssd1306_Init();
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();

    keypad_init();


    while (1) {
        if (!is_authenticated) {
            handle_authentication(); // Solicitar la clave si no está autenticado
        } else {
            // Verificar si ha pasado 1 minuto desde que el sistema se activó
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
                ssd1306_DrawBitmap(0, 0, locked_image_data, 128, 64, White);
                ssd1306_UpdateScreen();
                uart_send_string("\r\nPuerta cerrada después de 5 segundos\r\n");
                door_state = 0; // Estado de puerta cerrada
            }

            // Procesar comandos recibidos por UART
            process_commands();
        }
    }
}
/* USER CODE BEGIN 3 */
#define LOW_POWER_MODE 0 // 0: Normal, 1: Low Power Mode
#if LOW_POWER_MODE
    low_power_sleep_mode();
#endif
  
  /* USER CODE END 3 */



/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Configure the main internal regulator output voltage */
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the RCC Oscillators according to the specified parameters */
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
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}


