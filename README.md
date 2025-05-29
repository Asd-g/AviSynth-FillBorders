## Description

This is a simple filter that fills the borders of a clip, without changing the clip's dimensions.

This is [a port of the VapourSynth plugin FillBorders](https://github.com/dubhater/vapoursynth-fillborders).

### Requirements:

- AviSynth+ 3.6 or later

- Microsoft VisualC++ Redistributable Package 2022 (can be downloaded from [here](https://github.com/abbodi1406/vcredist/releases))

### Usage:

```
FillBorders (clip, int[] "left", int[] "top", int[] "right", int[] "bottom", int "mode", int "y", int "u", int "v", int "a", bool "interlaced", int "ts", int "ts_mode", value[] "fade_value")
```

The additional function FillMargins is alias for FillBordes(mode=0).

```
FillMargins (clip, int "left", int "top", int "right", int "bottom", int "y", int "u", int "v")
```

### Parameters:

- `clip`<br>
    A clip to process. All planar formats are supported.

- `left`, `top`, `right`, `bottom`<br>
    Number of pixels to fill on each side.
    - If a single integer is given, it's used for the first plane. Values for other planes are derived:
        - Chroma values are calculated by right-shifting the first plane's value by the plane's subsampling factor.
        - Alpha value uses the first plane's value directly.
    - If an array of 2 integers is given:
        - The first one is used for the first plane.
        - The second one is used for the second and third planes.
        - Alpha uses the first plane's value.
    - If an array of 3 integers is given:
        - They are used for the first three planes respectively.
        - Alpha uses the first plane's value.
    - If an array of 4 integers is given:
        - They are used for Y/R, U/G, V/B, A planes respectively.
    - Values must be non-negative. The total fill size (e.g., `left` + `right`) must not exceed the plane dimensions, with specific rules for `mode=2,3` (mirror/reflect) requiring twice the border size to be available.

    Default: left = 0, top = 0, right = 0, bottom = 0.

- `mode`<br>
    Specifies the filling algorithm:
    - `0`: "fillmargins" - Fills the borders exactly like the Avisynth filter FillMargins, version 1.0.2.0. This mode is similar to "repeat", except that each pixel at the top and bottom borders is filled with a weighted average of its three neighbours from the previous line.
    - `1`: "repeat" - Fills the borders using the outermost line or column.
    - `2`: "mirror" - Fills the borders by mirroring (half sample symmetric).
    - `3`: "reflect" - Fills the borders by reflecting (whole sample symmetric).
    - `4`: "wrap" - Fills borders by wrapping content from the opposite side. Can be combined with `ts` and `ts_mode` for smoothed transitions.
    - `5`: "fade" - Fills borders by creating a gradient. Behavior depends on `fade_value`.
    - `6`: "fixborders" - A direction "aware" modification of FillMargins. It also works on all four sides.

    Default: 0.

- `y`, `u`, `v`, `a`<br>
    Planes to process:
    - `1`: Return garbage.
    - `2`: Copy plane.
    -`3`: Process plane. Always process planes when the clip is RGB.

    Default: y = 3, u = 3, v = 3, a = 3.

- `interlaced`<br>
    Whether the clip is interlaced.<br>
    It's used `SeparateFields` and `Weave` to perform the filtering.<br>
    Default: False.

- `ts` (Transient Size)<br>
    Only active for `mode=4`.<br>
    Specifies the half-width of the transient smoothing area in pixels. The total smoothed region will be `2 * ts` pixels wide, straddling the border between wrapped content and original content.
    - `0`: No transient smoothing.
    - `1` to `5`: Enables transient smoothing. The value of `ts` must be less than or equal to the border size it's applied to (e.g., `ts <= left`). The maximum effective value for `2 * ts` is `10` (i.e., `ts` up to `5`).

    Default: `0`

- `ts_mode` (Transient Smoothing Mode)<br>
    Only active for `mode=4` and when `ts > 0`.<br>
    Determines the algorithm for transient smoothing:
    - `0`: Lerp Gradient. Filled border pixels within the `ts` zone are faded linearly towards the adjacent original pixel values. Original pixels are not modified.
    - `1`: Gaussian Blur (No Original Change). A Gaussian blur is applied to the `2 * ts` window around the border. Only the `ts` filled border pixels are updated with the blurred result. Original pixels are not modified.
    - `2`: Gaussian Blur (Originals Changed). A Gaussian blur is applied to the `2 * ts` window around the border. Both the `ts` filled border pixels and the `ts` adjacent original pixels are updated with the blurred result.

    Default: `1`

- `fade_value`<br>
    Only active for `mode=5`.<br>
    Determines the target of the fade.<br>
    - **Not defined / `fade_value=[-1]`:**
        The border pixels will fade towards the pixel values found in the **first row (row 0)** of the image plane.
        - Top/Bottom borders: Each column `x` fades towards `image[row=0, col=x]`.
        - Left/Right borders: Each pixel in the border fades towards `image[row=0, col=offset_within_border]`.
    - **Array of values (e.g., `[v1]`, `[vY, vU, vV]`, `[vR, vG, vB, vA]`):**
        The border pixels will fade towards these constant values for the respective planes.
        - **Type Consistency:** All elements in the array must be of the same type (all integers or all floats).
        - **Clip Type Matching:**
            - If array elements are **integers**: The input clip must be an integer format (8-16 bit). The provided integer values are assumed to be in the **native bit depth of the clip** (e.g., for a 10-bit clip, provide values from 0-1023). Values will be clamped to the clip's valid range.
            - If array elements are **floats**: The input clip must be a float format (32-bit). Floats are assumed to be normalized:
                - `0.0` to `1.0` for Luma (Y), RGB planes, and Alpha (A).
                - `-0.5` to `0.5` for Chroma planes (U, V). Values will be clamped.
        - **Array Size:**
            - `1 element`: The value is used for all processed planes. Alpha (if processed) will also use this value (appropriately scaled if it's YUV chroma to luma/alpha range).
            - `N elements` (where `N` is the number of planes in the clip, e.g., 3 for YUV/RGB, 1 for Gray): Values map one-to-one.
            - Other array sizes will result in an error.
    -   **Single Integer:** This is an error. Use an array `[value]` instead.

    Default: Not defined.

### Building:

```
Requirements:
    - Git
    - C++20 compiler
    - CMake >= 3.25
    - Ninja
```
```
git clone https://github.com/Asd-g/AviSynth-FillBorders
cd AviSynth-FillBorders
cmake -B build -G Ninja
ninja -C build
```
