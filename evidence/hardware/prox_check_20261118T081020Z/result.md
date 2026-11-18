# Proximity calibration 20261118T081020Z

- raw PS, hands away: {'n': 10, 'valid': 10, 'min': 0, 'max': 1023, 'median': 2.5}
- raw PS, hand 2-5 cm over the sensor: {'n': 10, 'valid': 10, 'min': 1023, 'max': 1023, 'median': 1023.0}
- board wizard: `#CAL,prox,ok,far_n=16,far_mean=2,far_min=0,far_max=10,near_n=16,near_min=1023,near_mean=1023,near_max=1023,thr_far=263,thr_near=516,calibrated=1`
- thresholds (from the board's measured statistics): far <= 263, near >= 516
- result: **PASS**
