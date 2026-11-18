# Final acceptance run

Firmware: dt-adfe15c9f6 (placeholder=0)

Set-up: handheld prototype: bare RT-Spark board, no enclosure, not mounted to an item

| id | requirement | status | evidence |
|---|---|---|---|
| R1 | >= 20 independent product trials per class | PASS | {'STATIONARY': 20, 'NORMAL_HANDLING': 20, 'ROUGH_HANDLING': 20} accuracy 0.933 |
| R2 | >= 50 timed on-board inferences (median, p95) | PASS | n=3021 median=286.12 us p95=287.81 us |
| R3 | sampling-to-response delay measured | PASS | sample->alert median 868.0 us p95 870 us; sample->decision median 710 us |
| R4 | >= 15 min continuous operation, no reset | PASS | 25.6 min, boots during run 0, stalls 0, missed samples 0, invalid 2111, log drops 0 |
| R5 | >= 5 consecutive minutes offline inference | PASS | longest uninterrupted inference stretch 24.7 min; operator network-off attestation True/True; host adapters all down start/end False/False |
| R6 | controlled failure with responsive scheduling and user feedback | PASS | {"fault_raised": true, "fault_raise_latency_s": 0.109, "status_commands_answered_during_fault": 10, "valid_predictions_during_fault": 0, "key_events_during_fault": 4, "fault_cleared": true, "valid_inference_resumed_after |
| R7 | exported model executes correctly on STM32 (golden held-out windows) | PASS | {"count": "30", "python_target_agree": "30", "target_correct": "24", "features_bit_exact": "30", "worst_feat_abs_diff": "0.000000", "cycles_min": "49562", "cycles_mean": "49653", "cycles_max": "49835", "us_mean": "295"} |
| R8 | touchless acknowledgement: >= 90 % of deliberate gestures, none while a hand stays near, none with hands away | PASS | 10/10 gestures acknowledged (median cue->ack 1.19 s); prox acks while hand held near 0; gestures with hands away 0; gestures during P1 handling [] |
| R9 | module diagnostics in the same firmware | PASS | 10 PASS / 0 FAIL / 0 WARN |
| R10 | heap / stack diagnostics recorded | PASS | heap min free 18192 B, stack HWM (words) {'sensor': 242, 'infer': 736, 'ui': 741, 'health': 572, 'console': 612} |
| R11 | integrated module functions during the run (microSD log, IR relay, wired) | INFO | microSD lines written 0 (mounted 0); IR tx_frames 76 rx_frames 76 relay 1; wire end #WIRE,can=0,can_tx_ok=0,can_tx_fail=0,can_ack_err=0,can_busy=0,tec=0,rec=0,lec=0,err_passive=0,bus_off=0,rs485=0,rs485_lines=0,rs485_rx_ |

## Trial-level confusion (rows = expected)

| expected | STILL | NORMAL | ROUGH | NO_DECISION |
|---|---|---|---|---|
| STILL | 20 | 0 | 0 | 0 |
| NORMAL | 1 | 16 | 3 | 0 |
| ROUGH | 0 | 0 | 20 | 0 |

| class | precision | recall |
|---|---|---|
| STATIONARY | 0.952 | 1.000 |
| NORMAL_HANDLING | 1.000 | 0.800 |
| ROUGH_HANDLING | 0.870 | 1.000 |
