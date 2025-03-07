#include "wifi.h"
#include "ring_buffer.h"
#include "usart.h"

extern ring_buffer_t rx_buffer;

void wifi_init(void) {
    // Inicialización del módulo WiFi
    // Aquí puedes agregar la configuración inicial del módulo WiFi
}

void wifi_process(void) {
    // Procesamiento de datos recibidos por WiFi
    uint8_t byte;
    while (ring_buffer_read(&rx_buffer, &byte)) {
        // Procesar los datos recibidos por WiFi
        // Por ejemplo, enviar los datos por UART
        HAL_UART_Transmit(&huart2, &byte, 1, 100);
    }
}