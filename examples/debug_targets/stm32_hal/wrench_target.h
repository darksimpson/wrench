#ifndef WRENCH_TARGET_H
#define WRENCH_TARGET_H

// wrench debug target glue for STM32Cube (HAL) projects, callable from C.
//
// main.c:
// #include "wrench_target.h"
// ...
// MX_USART2_UART_Init();
// wrench_target_init( &huart2 );
// while ( 1 )
// {
// 	wrench_target_poll();
// }
//
// and forward the UART callbacks (stm32xxxx_it.c or main.c):
// void HAL_UART_RxCpltCallback( UART_HandleTypeDef* huart ) { wrench_target_uart_rx( huart ); }
// void HAL_UART_ErrorCallback( UART_HandleTypeDef* huart ) { wrench_target_uart_error( huart ); }

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void wrench_target_init( UART_HandleTypeDef* huart );
void wrench_target_poll( void );
void wrench_target_uart_rx( UART_HandleTypeDef* huart );
void wrench_target_uart_error( UART_HandleTypeDef* huart );

#ifdef __cplusplus
}
#endif

#endif
