# Block diagram

Handheld prototype: bare RT-Spark board, no enclosure, not validated mounted to an item. The rendered Mermaid version is in [../product_design.md](../product_design.md). Plain-text version:

```
 SENSE                      PROCESS (STM32F407, FreeRTOS)                    ACT                 RECORD
 ICM-20608 --DRDY IRQ--> [sensor P5] --q_imu--> [infer P4] ---------+--> [ui P3] --> LCD ST7789   UART log (VCP)
   100 Hz accel+gyro       validate,             2 s window,         |      |   --> LED matrix    microSD CSV log
                           timestamp             12 features,        |      |   --> buzzer        W25Q event log (opt-in)
 AP3216C ---5 Hz--------> proximity gesture      decision tree       |      |   --> LEDs          RTC timestamps
   ALS / PS                detector (measured    + rule baseline     |      |
                           calibration only)          |              |   keys (EXTI): UP test alert, DOWN ack,
                                 \                     |             |                LEFT screen, RIGHT action
                                  +---> guardian logic (votes, cooldown, alerts, ack key/prox/IR/console, fault)
 AHT21 ---0.5 Hz--------> [health P2] IWDG supervision, env, diagnostics, SD / event log
 keys --EXTI----------------------------------------------------------> [ui P3]
                          [console P1] log stream -> USART1; commands; golden self-test
```
