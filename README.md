## Description

This is a simple filter that fills the borders of a clip, without changing the clip's dimensions.

This is [a port of the VapourSynth plugin FillBorders](https://github.com/dubhater/vapoursynth-fillborders).

### Requirements:

- AviSynth+ 3.6 or later

- Microsoft VisualC++ Redistributable Package 2022 (can be downloaded from [here](https://github.com/abbodi1406/vcredist/releases))

### Usage:

```
FillBorders (clip, int[] "left", int[] "top", int[] "right", int[] "bottom", int "mode", int "y", int "u", int "v", int "a", bool "interlaced")
```

The additional function FillMargins is alias for FillBordes(mode=0).

```
FillMargins (clip, int "left", int "top", int "right", int "bottom", int "y", int "u", int "v")
```

### Parameters:

- clip<br>
    A clip to process. All planar formats are supported.

- left, top, right, bottom<br>
    Number of pixels to fill on each side. These can be any non-negative numbers, within reason.<br>
    If they are all 0, the input clip is simply passed through.<br>
    For FillBorders:

        - These must be used as named parameters. For example, `FillBorders(left=1, top=1, right=1, bottom=1)`.
        - If a single value for `left`/`top`/`right`/`bottom` is specified, it will be used for alpha plane and it will be right shifted by subsampling factor for chroma planes.
        - If two values are given then the second value will be used for the third plane and the first value will be used for alpha plane.
        - If three values are given then the first value will be used for alpha plane.

    Default: left = 0, top = 0, right = 0, bottom = 0.

- mode (FillBorders only)<br>
    0: "fillmargins"<br>
        Fills the borders exactly like the Avisynth filter FillMargins, version 1.0.2.0. This mode is similar to "repeat", except that each pixel at the top and bottom borders is filled with a weighted average of its three neighbours from the previous line.<br>
    1: "repeat"<br>
        Fills the borders using the outermost line or column.<br>
    2: "mirror"<br>
        Fills the borders by mirroring (half sample symmetric).<br>
    3: "reflect"<br>
        Fills the borders by reflecting (whole sample symmetric).<br>
    4: "wrap"<br>
        Fills the borders by wrapping.<br>
    5: "fade"<br>
        Fill the borders to constant value.<br>
    6: "fixborders"<br>
        A direction "aware" modification of FillMargins. It also works on all four sides.<br>
    Default: 0.

- y, u, v, a<br>
    Planes to process.<br>
    1: Return garbage.<br>
    2: Copy plane.<br>
    3: Process plane. Always process planes when the clip is RGB.<br>
    Default: y = 3, u = 3, v = 3, a = 3.

- interlaced<br>
    Whether the clip is interlaced.<br>
    It's used `SeparateFields` and `Weave` to perform the filtering.<br>
    Default: False.

### Building:

```
Requirements:
    - Git
    - C++17 compiler
    - CMake >= 3.25
    - Ninja
```
```
git clone https://github.com/Asd-g/AviSynth-FillBorders
cd AviSynth-FillBorders
cmake -B build -G Ninja
ninja -C build
```
