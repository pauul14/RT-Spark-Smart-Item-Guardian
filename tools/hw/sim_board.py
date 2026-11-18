"""SIMULATED board for dry-running the session tools offline (--sim).

It mimics the firmware console protocol closely enough to exercise every code
path of capture.py, bringup.py and final_test.py without hardware. Its output
starts with a '#SIM' line, every log it produces is marked simulated, and the
tools refuse to store simulated runs as evidence (they go to evidence/sim/).
Nothing here is a measurement.
"""
import math
import random
import re
import time

from serial_common import utc_now

CLS = {"STATIONARY": "STILL", "NORMAL_HANDLING": "NORMAL", "ROUGH_HANDLING": "ROUGH"}


class SimBoard:
    def __init__(self, log_path, seed=4):
        self.rng = random.Random(seed)
        self.t = 1000.0                       # simulated monotonic seconds
        self.lines = []
        self.log = open(log_path, "a", encoding="utf-8", newline="\n")
        self.activity = "STATIONARY"          # set by the tool to mimic the operator
        self.capture = False
        self.seq = 0
        self.next_inf = self.t
        self.next_health = self.t
        self.fault = False
        self.rough_events = 0
        self.last_rough = -99
        self.alert = None                     # None / "TEST" / "ROUGH"
        self.alert_t = 0.0
        self.prox_cal = False
        self.hand_near = False
        self.prox_latched = False
        self.test_alerts = 0
        self.sd_lines = 3
        self.boot_t = self.t
        self.t -= 30.0                        # the board booted before the tool attached
        self._emit("#SIM,this is a simulated board - not evidence")
        self._emit("#BOOT,fw=SmartItemGuardian,version=1.0.0,built=sim,boot_count=1,reset=power-on,csr=0x0")
        self._emit("#CAL,gyro_bias,ok,12.00,-7.50,3.25")
        self.t += 30.0

    # ---- Board interface ----------------------------------------------------
    def _emit(self, text):
        item = (self.t, utc_now(), text)
        self.lines.append(item)
        self.log.write(f"{item[1]}\t{item[0]:.3f}\t{text}\n")

    def note(self, text):
        self._emit("#HOST," + text)

    def mark(self):
        return len(self.lines)

    def since(self, mark, prefix=None):
        out = [ln for ln in self.lines[mark:] if not ln[2].startswith("#HOST")]
        return [ln for ln in out if prefix is None or ln[2].startswith(prefix)]

    def sleep(self, dt):
        end = self.t + dt
        while self.t < end:
            step = min(0.01, end - self.t)
            self.t += step
            self._tick()

    def wait_for(self, pattern, timeout, mark=None):
        rx = re.compile(pattern)
        idx = self.mark() if mark is None else mark
        end = self.t + timeout
        while True:
            while idx < len(self.lines):
                if rx.search(self.lines[idx][2]):
                    return self.lines[idx]
                idx += 1
            if self.t >= end:
                return None
            self.sleep(0.05)

    def command(self, cmd, expect, timeout=5.0):
        m = self.mark()
        self.send(cmd)
        return self.wait_for(expect, timeout, m)

    def close(self):
        self.log.close()

    # ---- simulated firmware ---------------------------------------------------
    def send(self, cmd):
        self.note("send," + cmd)
        a = cmd.split()
        self._emit("#CMD," + a[0])
        c = a[0]
        if c == "ver":
            self._emit("#VER,fw=SmartItemGuardian,version=1.0.0,built=sim,model=sim-model,placeholder=0,model_bytes=200,"
                       "nodes=13,depth=4,freertos=V10.3.1,hal=0x01080300")
        elif c == "cap" and a[1] == "start":
            self.capture = True
            self._emit(f"#CAP,START,label={a[2]},session={a[3]},trial={a[4]},t_ms=1,fs_hz=100,acc_lsb_per_g=4096,"
                       "gyro_lsb_per_dps=32.8,gyro_bias=12.000;-7.500;3.250,bias_ok=1,fw=1.0.0,model=sim-model")
        elif c == "cap" and a[1] == "stop":
            self.capture = False
            self._emit("#CAP,STOP,1,samples=0,t_ms=1,log_drop_lines=0")
        elif c == "golden":
            self._emit("#GOLDEN,SUMMARY,count=30,python_target_agree=30,target_correct=27,features_bit_exact=30,"
                       "worst_feat_abs_diff=0.000000000,cycles_min=9000,cycles_mean=9500,cycles_max=9900,us_mean=56")
        elif c == "lat":
            if len(a) == 1:
                for n in ("inference_us", "sample_to_decision_us", "sample_to_response_us", "key_to_feedback_us"):
                    self._emit(f"#LAT,{n},n=200,total=900,min=50,median=57,p95=61,max=70")
            else:
                self._emit("#LAT,reset")
        elif c == "fault":
            self.fault = a[2] == "on"
            self._emit(f"#FAULTINJ,imu,{a[2]},t_ms=1")
            self.sleep(0.12)
            self._emit("#FAULT,imu,raised,consecutive_invalid=10,injected=1" if self.fault else "#FAULT,imu,cleared")
        elif c == "diag":
            tags = {"imu": "imu", "als": "als_ps", "aht": "aht21", "flash": "spi_flash", "sd": "sdcard", "rtc": "rtc",
                    "ir": "ir", "wifi": "wifi_rw007", "codec": "codec_es8388", "can": "can1", "mcu": "mcu_adc",
                    "i2cscan": "i2c2_scan"}
            which = a[1] if len(a) > 1 else "all"
            if which in ("lcd", "matrix", "buzzer", "leds"):
                self._emit(f"#DIAG,{which},DONE,sim")
            for name, tag in tags.items():
                if which in ("all", name):
                    self._emit(f"#DIAG,{tag},PASS,sim")
            if which == "all":
                self._emit("#DIAG,all,DONE")
        elif c == "alert" and len(a) > 1 and a[1] == "test":
            if self.alert == "ROUGH":
                self._emit("#ALERT,test,not_raised,reason=higher_priority_alert_active")
            else:
                self.alert, self.alert_t = "TEST", self.t
                self.test_alerts += 1
                self.sd_lines += 1
                self._emit(f"#EVT,1,TEST_ALERT,UNKNOWN,{self.test_alerts}")
                self._emit(f"#RESP,test,s2r_us={self.rng.randint(80, 200)},s2lcd_us={self.rng.randint(8000, 15000)}")
        elif c == "ack":
            self._ack(3)
        elif c == "time":
            self._emit("#TIME,set,ok")
            self._emit("#DIAG,rtc,PASS,source=LSE,time_valid=1,now=2026-11-17 12:00:00")
        elif c == "prox" and len(a) > 1 and a[1] == "cal":
            self._emit("#CAL,prox,started: hands AWAY, then hold a hand 2-5 cm over the sensor when the screen says so")
            self.sleep(12.0)
            self.prox_cal, self.prox_latched = True, self.hand_near   # fresh detector: re-arms once FAR
            self._emit("#CAL,prox,ok,far_n=15,far_mean=6,far_min=3,far_max=11,near_n=15,near_min=310,near_mean=420,"
                       "near_max=520,thr_far=85,thr_near=160,calibrated=1")
        elif c == "prox" and len(a) > 1 and a[1] == "clear":
            self.prox_cal = False
            self._emit("#CAL,prox,cleared (touchless acknowledgement disabled until 'prox cal')")
        elif c == "prox":
            ps = 420 if self.hand_near else 6
            self._emit(f"#DIAG,als_ps,PASS,als=300,lux=105.0,ps={ps},ir=3,ps_valid=1,"
                       f"prox_zone={'NEAR' if self.hand_near else 'FAR'},prox_cal={'measured' if self.prox_cal else 'none'},"
                       "thr_far=85,thr_near=160,far_max=11,near_min=310,gestures=0,invalid_ps=0")
        elif c in ("say", "ask"):
            if "paired for TEST" in cmd:
                self._emit("#IR,rx,addr=0x0000,cmd=0x45,action=TEST ALERT")
            self.prompt_id = getattr(self, "prompt_id", 0) + 1
            self._emit(f"#PROMPT,{self.prompt_id},{c},{a[1]}")
            if c == "ask":
                self.sleep(0.4)
                self._emit(f"#ANSWER,{self.prompt_id},UP,remaining_ms=1000")
        elif c == "cue":
            pass
        elif c == "ir":
            if len(a) > 1 and a[1] == "learn":
                for k in ("TEST ALERT", "ACK", "NEXT SCREEN"):
                    self.sleep(0.5)
                    self._emit(f"#IR,paired,{k},addr=0x0000,cmd=0x45")
                self._emit("#IR,pairing,done,test=0x80000045,ack=0x80000046,next=0x80000047")
            else:
                self._emit("#IR,status,rx_idle=1,rx_edges=0,rx_frames=0,rx_errors=0,own_test_echoes=0,tx_frames=0,relay=1")
        elif c == "sd":
            self._emit(f"#SDLOG,enabled=1,mounted=1,card_present=1,cd_pin=0,last_result=OK,lines={self.sd_lines},errors=0,"
                       "read_err=0,write_err=0")
        elif c == "wire":
            on = len(a) > 2 and a[2] == "on"
            self._emit(f"#WIRE,can={int(on and a[1] == 'can')},can_tx_ok=0,can_tx_fail=2,can_ack_err=2,can_busy=0,tec=16,"
                       f"rec=0,lec=3,err_passive=0,bus_off=0,rs485={int(on and a[1] == 'rs485')},rs485_lines=2,"
                       "rs485_rx_bytes=0,rs485_queries=0")
        elif c in ("tasks", "heap", "cnt", "status", "cal", "screen", "verbose", "log"):
            self._emit(f"#{c.upper()},sim")
            if c == "cal":
                self._emit("#CAL,gyro_bias,ok,12.00,-7.50,3.25")

    def _ack(self, src):
        if self.alert:
            self.alert = None
            self._emit(f"#EVT,1,ACK,UNKNOWN,{src}")
            self._emit(f"#RESP,ack,src={['key', 'prox', 'ir', 'console'][src]},ack_to_feedback_us={self.rng.randint(90, 400)}")

    def press_key(self, name):
        self._emit(f"#KEY,{name},down,feedback_us={self.rng.randint(60, 140)}")
        if name == "DOWN":
            self._ack(0)

    def hand(self, near, hold_s=0.8):
        """Operator moves a hand over (near=True) or away from the sensor; mimics prox_ack.c:
        a gesture needs FAR first and fires once per approach (0.6 s hold)."""
        if near and not self.hand_near:
            self.hand_near = True
            self.sleep(min(hold_s, 0.6))
            if self.prox_cal and not self.prox_latched and hold_s >= 0.6:
                self.prox_latched = True
                if not self.alert:
                    why = "no_alert"
                elif self.t - self.alert_t < 1.0:
                    why = "alert_too_new"
                else:
                    why = "ack"
                self._emit(f"#PROX,gesture,{why},ps=420,t_us=1")
                self._emit(f"#EVT,1,PROX_GESTURE,UNKNOWN,{int(why == 'ack')}")
                if why == "ack":
                    self._ack(1)
            self.sleep(max(0.0, hold_s - 0.6))
        elif not near and self.hand_near:
            self.hand_near = False
            self.sleep(0.7)
            self.prox_latched = False

    def _tick(self):
        if self.capture:
            self.seq += 1
            k = {"STATIONARY": 0.0, "NORMAL_HANDLING": 0.2, "ROUGH_HANDLING": 1.6}[self.activity]
            ph = 2 * math.pi * self.t * (4 if k > 1 else 1.5)
            ax = int(k * 4096 * math.sin(ph) + self.rng.gauss(0, 10))
            az = int(4096 + k * 2000 * math.cos(ph) + self.rng.gauss(0, 10))
            gx = int(12 + k * 8000 * math.sin(ph / 2) + self.rng.gauss(0, 6))
            self._emit(f"S,{self.seq},{int(self.t * 1e6) & 0xFFFFFFFF},{ax},0,{az},{gx},-7,3,0")
        if self.t >= self.next_inf:
            self.next_inf += 0.5
            cls = CLS[self.activity] if not self.fault else "UNKNOWN"
            if self.rng.random() < 0.05 and not self.fault:
                cls = self.rng.choice(list(CLS.values()))
            self._emit(f"#INF,{self.seq},{int(self.t * 1000)},{0 if self.fault else 1},{cls},95,{cls},{cls},"
                       f"{self.rng.randint(8800, 10500)},{self.rng.randint(400, 900)},1")
            if cls == "ROUGH" and self.t - self.last_rough >= 5:
                self.rough_events += 1
                self.alert, self.alert_t = "ROUGH", self.t
                self._emit(f"#EVT,1,ROUGH,ROUGH_HANDLING,{self.rough_events}")
                self._emit(f"#RESP,rough,s2r_us={self.rng.randint(900, 2500)},s2lcd_us={self.rng.randint(8000, 15000)}")
            if cls == "ROUGH":
                self.last_rough = self.t
        if self.t >= self.next_health:
            self.next_health += 10
            self._emit(f"#HEALTH,t_ms={int((self.t - self.boot_t) * 1000)},heap_free=21000,heap_min=20500,hwm_sensor=300,"
                       "hwm_infer=700,hwm_ui=600,hwm_health=650,hwm_console=800,samples=1,invalid=0,missed=0,"
                       "drdy_to=0,qfull=0,i2c_err=0,i2c_rec=0,swi2c_err=0,log_drop=0,uart_ovr=0,iwdg=1,stall=0")
