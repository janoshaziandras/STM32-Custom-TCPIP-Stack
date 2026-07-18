#include "headers.h"
#include "main.h"

#include <stdarg.h>
#include <stdio.h>

volatile uint16_t head = 0;
volatile uint16_t tail = 0;
char console_buffer[CONSOLE_BUF_SIZE];

void writeline(const char* fmt, ...) {
    char temp[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    if (len > 0) {
        for (int i = 0; i < len; i++) {
            console_buffer[head] = temp[i];
            head = (head + 1) % CONSOLE_BUF_SIZE;
        }
        console_buffer[head] = '\r'; head = (head + 1) % CONSOLE_BUF_SIZE;
        console_buffer[head] = '\n'; head = (head + 1) % CONSOLE_BUF_SIZE;
    }
}

void console_process(void) {

    while (tail != head) {

        if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TXE)) {
            huart3.Instance->TDR = console_buffer[tail];
            tail = (tail + 1) % CONSOLE_BUF_SIZE;
        } else {
            break;
        }
    }
}
