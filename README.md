# Description

This is a simple filter that fills the borders of a clip, without changing the clip's dimensions.

This is [a port of the VapourSynth plugin FillBorders](https://github.com/dubhater/vapoursynth-fillborders).

# Usage

```
FillBorders (clip, int "left", int "top", int "right", int "bottom", int "mode")
```

```
FillMargins (clip, int "left", int "top", int "right", int "bottom", 0)
```


## Parameters:

- clip\
    A clip to process. It must have constant format and dimensions and it must be 8..16 bit.

- left, right, top, bottom\
    Number of pixels to fill on each side. These can be any non-negative numbers, within reason. If they are all 0, the input clip is simply passed through.\
    Default: 0.

- mode\
    0: "fillmargins"\
        Fills the borders exactly like the Avisynth filter FillMargins, version 1.0.2.0. This mode is similar to "repeat", except that each pixel at the top and bottom borders is filled with a weighted average of its three neighbours from the previous line.\
    1: "repeat"\
        Fills the borders using the outermost line or column.\
    2: "mirror"\
        Fills the borders by mirroring.\
    Default: 0

### Note: A function FillMargins is alias for FillBordes(mode=0).
