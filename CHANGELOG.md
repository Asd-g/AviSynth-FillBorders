##### 1.5.0:
    Improved lerp precision (`mode=5`).
    Fixed out of bound access (`mode=6`).
    Added parameters `ts`, `ts_mode`, `fade_value`.
    Fixed `y/u/v/a=1`.

##### 1.4.2:
    Added parameter `interlaced`.

##### 1.4.1:
    Fixed set of memory for high bit depth video.

##### 1.4.0:
    Dropped support for AviSynt 2.6.
    Changed type of parameters `left`, `top`, `right`, `bottom` to arrays. (FillBorders only)
    Added support for alpha plane.

##### 1.3.0:
    Added mode 6 (fixborders) (from vs FillBorders).

##### 1.2.1:
    Not allowed clips with _FieldBased > 0.

##### 1.2.0:
    Added additional modes (from ffmpeg): 3 (reflect); 4 (wrap); 5 (fade).

##### 1.1.1:
    Throw error for non-planar formats.

##### 1.1.0:
    Added support for float.
    Added y, u, v, parameters.
    Registered as MT_NICE_FILTER.
    Removed parameter mode from FillMargins.

##### 1.0.0:
    Port of the VapourSynth plugin FillBorders.