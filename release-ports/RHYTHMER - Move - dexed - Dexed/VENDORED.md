# Original Dexed native adapter

Original DSP binary is reused byte-for-byte from the pinned release. Native loader and control metadata adapt the public API; no JAZZY DSP is used. Existing Open MPC wrapper and Rhythmer rack staging are retained. File-picker controls and read-only telemetry are not mapped to editable floats. Full preset/asset source review and host acceptance are separate gates.

Local candidate repair: Init transpose byte 143 -> 144; see LOCAL_REPAIR.json and corresponding-source/. Original bank data is unchanged.

Local candidate repairs: Init transpose byte 143 -> 144; preserve original seven-bit operator Fine values on state restore. See LOCAL_REPAIR.json and corresponding-source/. Original bank data is unchanged; live edit ranges are unchanged.
