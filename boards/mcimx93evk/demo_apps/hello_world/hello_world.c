/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #include "fsl_device_registers.h"
 #include "fsl_debug_console.h"
 #include "pin_mux.h"
 #include "board.h"
 #include <string.h>  // for memcpy
 
 /*******************************************************************************
  * Definitions
  ******************************************************************************/
 
 #define OCRAM_OUTPUT_ADDR 0x20484000U
 #define OCRAM_OUTPUT_LEN  128U
 
 /* 要写入 OCRAM 的 128 字节数据 */
 static const unsigned char output_data[OCRAM_OUTPUT_LEN] = {
     0x30,0x00,0x00,0x00,0x0c,0xee,0x0e,0xf0,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,
     0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,
     0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,
     0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,0x0d,0xef,
     0x12,0xf0,0x26,0xdd,0x00,0x00,0x00,0x00,0x04,0x01,0x02,0x03,0x05,0x07,0x0c,0x01,
     0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
 };
 
 /*!
  * @brief Main function
  */
 int main(void)
 {
     char ch;
 
     /* Init board hardware. */
     BOARD_InitBootPins();
     BOARD_BootClockRUN();
     BOARD_InitDebugConsole();
 
     /* 原有启动提示 */
     PRINTF("hello world.\r\n");
 
     /* ------------------------------------------------------------------ */
     /* 将 output_data 写入 OCRAM，然后打印确认 */
     {
         volatile uint8_t *dest = (uint8_t *)OCRAM_OUTPUT_ADDR;
 
         /* 使用 memcpy 将数据写入物理地址 */
         memcpy((void *)dest, output_data, OCRAM_OUTPUT_LEN);
 
         /* 如果 M 核启用了 D-Cache，并需要保证与其他核一致性，可使用 CMSIS 接口清理缓存：
          * SCB_CleanInvalidateDCache_by_Addr((uint32_t *)dest, OCRAM_OUTPUT_LEN);
          */
 
         PRINTF("Wrote %u bytes to OCRAM at 0x%08X\r\n",
                (unsigned)OCRAM_OUTPUT_LEN, (unsigned)OCRAM_OUTPUT_ADDR);
     }
     /* ------------------------------------------------------------------ */
 
     /* 保持原有的回环逻辑，方便通过串口交互 */
     while (1)
     {
         ch = GETCHAR();
         PUTCHAR(ch);
     }
 }
 