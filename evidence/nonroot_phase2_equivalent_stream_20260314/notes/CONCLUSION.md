# Conclusion

This validation shows that the first-layer repository can collect a non-root IMU stream that is compatible with the second-stage data requirements.

Reliable findings:

- The stream schema matches the second-stage sensor CSV expectation:
  `timestamp_ns,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z`
- Five repeated short runs stayed at approximately `200 Hz` total row rate
- One `60 s` long run also stayed at approximately `200 Hz`
- The second-stage gates are therefore satisfied:
  - `single_key`: `190 Hz`
  - `free_type`: `150 Hz`

Interpretation:

- The second-stage *data requirement* no longer depends on root on this Tahoe machine
- The current second-stage *implementation* still depends on root because it uses the old `macimu`-based collector path and an explicit root check
- The first-layer proof path uses a different backend: direct `AppleSPUHIDDevice` access
