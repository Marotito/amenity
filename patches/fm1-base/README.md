# Patches to fm1-base

fm1-base (`base/`, a submodule) is not ours; what Amenity+ needs changed in it lives here, applied by
`.github/workflows/dev.yml` before the build (`git -C base apply`).

- `0001-audio-at-power-on.patch`: the audio (ALNK0, zeros) starts right after the power setup instead of after
  `app_init`, so the DAC is clocked and fed silence from the first moment. Marote, 17 nov: Amenity (and so every
  fm1-base app) pops at power-on, the stock firmware does not. The interrupt still comes with the others, after
  `app_init`; safe mode keeps no audio. To be confirmed on the FM-1: if it does not help, drop it.
