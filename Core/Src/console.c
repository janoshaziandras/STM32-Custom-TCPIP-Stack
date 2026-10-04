#include "headers.h"
#include "main.h"

#include <stdio.h>
#include <stdarg.h>

static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;
static char console_buffer[APP_CONSOLE_BUFFER_CAPACITY];

void writeline(const char* fmt, ...) {
    char temp[APP_CONSOLE_FORMAT_BUFFER_CAPACITY];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    if (len > 0) {
        for (int i = 0; i < len; i++) {
            console_buffer[head] = temp[i];
            head = (head + 1) % APP_CONSOLE_BUFFER_CAPACITY;
        }
        console_buffer[head] = '\r'; head = (head + 1) % APP_CONSOLE_BUFFER_CAPACITY;
        console_buffer[head] = '\n'; head = (head + 1) % APP_CONSOLE_BUFFER_CAPACITY;
    }
}

void console_process(void) {

    while (tail != head) {

        if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TXE)) {
            huart3.Instance->TDR = console_buffer[tail];
            tail = (tail + 1) % APP_CONSOLE_BUFFER_CAPACITY;
        } else {
            break;
        }
    }
}
