# Bring-up 20261118T050538Z

| item | status | evidence | note |
|---|---|---|---|
| boot banner / reset cause | PASS | #BOOT,fw=SmartItemGuardian,version=1.0.0,built=Nov 18 2026 02:02:42,boot_count=1,reset=software,csr=0x1e000000 |  |
| boot log BOOT,fw | INFO | #BOOT,fw=SmartItemGuardian,version=1.0.0,built=Nov 18 2026 02:02:42,boot_count=1,reset=software,csr=0x1e000000 |  |
| boot log CLOCK | INFO | #CLOCK,hse_ok=1,sysclk=168000000,hclk=168000000,pclk1=42000000,pclk2=84000000 |  |
| boot log INIT | INFO | #INIT,lcd,ok,id=0x81b3 |  |
| boot log INIT | INFO | #INIT,matrix,ok,leds=19 |  |
| boot log INIT | INFO | #INIT,imu,ok,whoami=0xaf,config_readback=ok |  |
| boot log INIT | INFO | #INIT,als_ps,ok |  |
| boot log INIT | INFO | #INIT,aht21,ok,status=0x18 |  |
| boot log INIT | INFO | #INIT,spi_flash,ok,jedec=ef4017,size=8388608 |  |
| boot log INIT | INFO | #INIT,rtc,FAIL,source=none,was_running=0,time_valid=0 |  |
| boot log INIT | INFO | #INIT,ir,rx_idle=1,relay=1,paired_arm=0,paired_ack=0,paired_next=0 |  |
| boot log INIT | INFO | #INIT,model,version=ph-fd5d5654a0,placeholder=1,nodes=5,max_depth=2,bytes=55 |  |
| boot log RTOS | INFO | #RTOS,kernel=V10.3.1,heap_total=49152,heap_free_before_start=18944,tick_hz=1000 |  |
| boot log RTOS | INFO | #RTOS,scheduler_running,tasks=6,iwdg_timeout_ms~8190 |  |
| console + firmware identity | PASS | #VER,fw=SmartItemGuardian,version=1.0.0,built=Nov 18 2026 02:02:40,model=ph-fd5d5654a0,placeholder=1,model_bytes=55,nodes=5,depth=2,freertos=V10.3.1,hal=0x01080 |  |
| FreeRTOS task/heap | INFO | #TASK,console,prio=1,state=X,stack_hwm_words=774,runtime_pct=0.0 |  |
| FreeRTOS task/heap | INFO | #TASK,IDLE,prio=0,state=R,stack_hwm_words=149,runtime_pct=36.3 |  |
| FreeRTOS task/heap | INFO | #TASK,sensor,prio=5,state=B,stack_hwm_words=386,runtime_pct=57.9 |  |
| FreeRTOS task/heap | INFO | #TASK,health,prio=2,state=B,stack_hwm_words=708,runtime_pct=0.5 |  |
| FreeRTOS task/heap | INFO | #TASK,infer,prio=4,state=B,stack_hwm_words=923,runtime_pct=0.0 |  |
| FreeRTOS task/heap | INFO | #TASK,ui,prio=3,state=B,stack_hwm_words=779,runtime_pct=5.0 |  |
| FreeRTOS task/heap | INFO | #HEAP,total=49152,free=18192,min_ever_free=18192 |  |
| diag imu | PASS | #DIAG,imu,PASS,whoami=0xaf,config=verified,drdy=irq,raw=-159,-123,4105,27,-36,1,die_temp=37.6C,samples=261,invalid=0 |  |
| diag als | PASS | #DIAG,als_ps,PASS,als=30,lux=10.5,ps=5,ir=30,ps_valid=1,lid=OPEN,cal=default,signal=ALS,thr=10..30 |  |
| diag aht | PASS | #DIAG,aht21,PASS,temp=31.42C,rh=81.37%,init_status=0x18,errors=0 |  |
| diag flash | PASS | #DIAG,spi_flash,PASS,jedec=ef4017,size=8388608,sr1=0x00,first8=00000000f00ffff7 (read-only) |  |
| diag sd | N/A | #DIAG,sdcard,N/A,SD: CD=1, init failed (err 0x00000004) - no card or unreadable |  |
| diag rtc | FAIL | #DIAG,rtc,FAIL,source=none,time_valid=0,now=4614-00-00 233:116:00 |  |
| diag wifi | PASS | #DIAG,wifi_rw007,PASS,RW007: INT before=1 after=1 transitions_1.5s=2 first_at=1ms (presence/boot signal only; no network joined) |  |
| diag codec | PASS | #DIAG,codec_es8388,PASS,ES8388: ACK 0x10, reg00..04 = 06 5c c3 fc c0 (read-only; audio path unused) |  |
| diag can | FAIL | #DIAG,can1,FAIL,CAN1: silent loop-back 500k frame not received (MCU controller only; no bus attached) |  |
| diag mcu | PASS | #DIAG,mcu_adc,PASS,MCU: die_temp=42.6C vdda=3.302V (factory calibration) |  |
| diag i2cscan | INFO | #DIAG,i2c2_scan,found=0x10 0x1e 0x68 (expected 0x10 ES8388, 0x1e AP3216C, 0x68 ICM-20608) |  |
| IMU sampling interval | PASS | n=1010 median_dt_us=10000 min=9999 max=10000 missing=0 polled_flag=0 |  |
| lcd operator check | NO-ANSWER | #DIAG,lcd,DONE,full_screen_fill_us=13822,id=0x81b3 (operator confirms R,G,B,W,K bars) |  |
| matrix operator check | NO-ANSWER | #DIAG,matrix,DONE,frames=517 (operator confirms colours R,G,B,W and the 19-LED chase) |  |
| buzzer operator check | NO-ANSWER | #DIAG,buzzer,DONE,pattern=rough (operator confirms 3 beeps) |  |
| leds operator check | NO-ANSWER | #DIAG,leds,DONE (operator confirms red/blue alternate x3) |  |
| key UP | FAIL | no #KEY line within 25 s |  |
| key DOWN | FAIL | no #KEY line within 25 s |  |
| key LEFT | FAIL | no #KEY line within 25 s |  |
| key RIGHT | FAIL | no #KEY line within 25 s |  |
| ALS/PS uncovered | INFO | #DIAG,als_ps,PASS,als=25,lux=8.8,ps=4,ir=18,ps_valid=1,lid=OPEN,cal=default,signal=ALS,thr=10..30 |  |
| ALS/PS covered | INFO | #DIAG,als_ps,PASS,als=23,lux=8.1,ps=4,ir=16,ps_valid=1,lid=OPEN,cal=default,signal=ALS,thr=10..30 |  |
| ALS/PS uncovered again | INFO | #DIAG,als_ps,PASS,als=23,lux=8.1,ps=22,ir=20,ps_valid=1,lid=OPEN,cal=default,signal=ALS,thr=10..30 |  |
| AP3216C responds to cover (PS up or ALS down by >= 12 counts) | FAIL | ps=[4, 4, 22] als=[25, 23, 23] |  |
| lid calibration | FAIL | #CAL,lid,started: close the lid within 5 s, then open it when the screen says OPEN |  |
| microSD event log | PENDING | #SDLOG,enabled=1,mounted=0,card_present=0,cd_pin=1,last_result=NOT_READY,lines=0,errors=0,read_err=0,write_err=0 - insert a FAT32 microSD card to verify (it is  |  |
| IR emitter -> receiver decoded self-echo | PASS | #DIAG,ir,PASS,IR: rx_idle=HIGH(ok) tx=sent decoded_self_echo=1 rx_edges=68 frames_total=3 decode_errors=1 (echo needs a reflector above the board) |  |
| IR remote pairing/control | PENDING | no remote available |  |
| CAN telemetry | PARTIAL | #WIRE,can=1,can_tx_ok=0,can_tx_fail=1,can_ack_err=1,can_busy=0,tec=8,rec=0,lec=3,err_passive=0,bus_off=0,rs485=0,rs485_lines=0,rs485_rx_bytes=0,rs485_queries=0 | frames sent but not acknowledged: no bus partner (expected without a second node) |
| RS485 telemetry | PARTIAL | #WIRE,can=0,can_tx_ok=0,can_tx_fail=1,can_ack_err=1,can_busy=0,tec=8,rec=0,lec=3,err_passive=0,bus_off=0,rs485=1,rs485_lines=2,rs485_rx_bytes=0,rs485_queries=0 | lines transmitted; reception needs a partner/USB-RS485 adapter |
| on-target model self-test (golden) | INFO | #GOLDEN,SUMMARY,count=30,python_target_agree=30,target_correct=30,features_bit_exact=30,worst_feat_abs_diff=0.000000,cycles_min=106692,cycles_mean=114141,cycles |  |
| counters | INFO | #LAT,inference_us,n=256,total=441,min=629,median=631,p95=633,max=637 |  |
| counters | INFO | #LAT,sample_to_decision_us,n=256,total=441,min=1053,median=1056,p95=1057,max=1062 |  |
| counters | INFO | #LAT,sample_to_response_us,n=2,total=2,min=1210,median=1210,p95=1212,max=1212 |  |
| counters | INFO | #LAT,key_to_feedback_us,n=9,total=9,min=10,median=11,p95=21961,max=21961 |  |
| counters | INFO | #LAT,last_inference_cycles=106524,cpu_hz=168000000 |  |
| counters | INFO | #CNT,imu_samples=22386,imu_invalid=0,imu_bus_err=0,imu_stuck=0,imu_missed=0,drdy_timeout=0,imu_qfull=0,imu_reinit=0,als_err=0,aht_err=0,i2c2_err=0,i2c2_recover= |  |
| counters | INFO | #CNT,log_drop_lines=0,log_drop_bytes=0,uart_rx_overrun=0,uart_rx_dropped=0,ui_frames=1096,key_events=9,iwdg_refresh=222,task_stall=0,evlog_written=0,evlog_faile |  |
| counters | INFO | #IR,status,rx_idle=1,rx_edges=604,rx_frames=3,rx_errors=2,own_test_echoes=1,tx_frames=3,relay=1,arm=0x00000000,ack=0x00000000,next=0x00000000 |  |
