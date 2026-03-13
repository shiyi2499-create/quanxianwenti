# Requirement Mapping

Second-stage requirement source:
- `/Users/shiyi/备份（mac_vs专用）/README.md`
- `/Users/shiyi/备份（mac_vs专用）/collector.py`
- `/Users/shiyi/备份（mac_vs专用）/sensor_reader.py`
- `/Users/shiyi/备份（mac_vs专用）/preprocessor.py`

Mapped requirements:

1. IMU schema
- required: `timestamp_ns,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z`
- satisfied by: `exp7_phase2_capture.c`

2. Timing model
- required: monotonic `timestamp_ns` suitable for alignment with key events in the collector
- satisfied by: `exp7_phase2_capture.c` using monotonic timestamps per emitted row

3. Rate regime
- required: roughly `190 Hz` for `single_key`, `150 Hz` for `free_type`
- satisfied by: repeated short runs and one long run at approximately `200 Hz`

4. Duration / persistence
- required: long enough to support realistic sessions, not only a tiny proof-of-concept burst
- satisfied here by: a stable `60 s` run at approximately `200 Hz`

5. Caveat
- not proven here: unchanged second-stage collector execution without root
- proven here: the IMU stream needed by that collector can be produced without root through a different backend
