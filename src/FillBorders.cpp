#include <memory>

#include "avisynth.h"
#include "avs/minmax.h"

template <typename T>
static inline void memset16(T* ptr, T value, size_t num)
{
    while (num-- > 0)
        *ptr++ = value;
}

template<typename T, typename T1>
static auto lerp(const T1 fill, const T1 src, const int pos, const int size, const int bits, const int plane)
{
    if constexpr (std::is_same_v<T, uint8_t>)
        return clamp(((fill * 256 * pos / size) + (src * 256 * (size - pos) / size)) >> 8, 0, 255);
    else if constexpr (std::is_same_v<T, uint16_t>)
    {
        const int64_t max_range = 1LL << bits;
        return static_cast<int>(clamp(((fill * max_range * pos / size) + (src * max_range * (static_cast<int64_t>(size) - pos) / size)) >> bits, static_cast<int64_t>(0), max_range - 1));
    }
    else
        return clamp(((fill * pos / size) + (src * (size - pos) / size)), plane ? -0.5f : 0.0f, plane ? 0.5f : 1.0f);
}

class FillBorders : public GenericVideoFilter
{
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
    int m_mode;
    int process[3];
    bool has_at_least_v8;

    template<typename T, typename T1>
    PVideoFrame fill(PVideoFrame frame, IScriptEnvironment* env);

    PVideoFrame (FillBorders::* processing)(PVideoFrame frame, IScriptEnvironment* env);

public:
    FillBorders(PClip _child, int left, int top, int right, int bottom, int mode, int y, int u, int v, IScriptEnvironment* env);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
};

template<typename T, typename T1>
PVideoFrame FillBorders::fill(PVideoFrame frame, IScriptEnvironment* env)
{
    env->MakeWritable(&frame);
    const int size = vi.ComponentSize();
    const int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    const int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    const int* planes = vi.IsRGB() ? planes_r : planes_y;
    const int planecount = min(vi.NumComponents(), 3);
    for (int i = 0; i < planecount; ++i)
    {
        const int plane = planes[i];
        const int height = frame->GetHeight(plane);

        if (process[i] == 3)
        {
            const int stride = frame->GetPitch(plane) / size;
            const int width = frame->GetRowSize(plane) / size;
            T* dstp = reinterpret_cast<T*>(frame->GetWritePtr(plane));

            const int left = m_left >> vi.GetPlaneWidthSubsampling(plane);
            const int top = m_top >> vi.GetPlaneHeightSubsampling(plane);
            const int right = m_right >> vi.GetPlaneWidthSubsampling(plane);
            const int bottom = m_bottom >> vi.GetPlaneHeightSubsampling(plane);

            switch (m_mode)
            {
                case 0:
                    if constexpr ((std::is_same_v<T, uint8_t>))
                    {
                        for (int y = top; y < height - bottom; ++y)
                        {
                            memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                            memset(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                        }
                    }
                    else
                    {
                        for (int y = top; y < height - bottom; ++y)
                        {
                            memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                            memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                        }
                    }

                    for (int y = top - 1; y >= 0; y--)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y + 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8LL * size);

                        // weighted average for the rest
                        for (int x = 1; x < width - 8; ++x)
                        {
                            T prev = dstp[stride * (y + 1) + x - 1];
                            T cur = dstp[stride * (y + 1) + x];
                            T next = dstp[stride * (y + 1) + x + 1];

                            if constexpr (std::is_integral_v<T>)
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                            else
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4 / 2147483647.f) / 8;
                        }
                    }

                    for (int y = height - bottom; y < height; ++y)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y - 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8LL * size);

                        // weighted average for the rest
                        for (int x = 1; x < width - 8; ++x)
                        {
                            T prev = dstp[stride * (y - 1) + x - 1];
                            T cur = dstp[stride * (y - 1) + x];
                            T next = dstp[stride * (y - 1) + x + 1];

                            if constexpr ((std::is_integral_v<T>))
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                            else
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4 / 2147483647.f) / 8;
                        }
                    }
                    break;
                case 1:
                    if constexpr ((std::is_same_v<T, uint8_t>))
                    {
                        for (int y = top; y < height - bottom; ++y)
                        {
                            memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                            memset(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                        }
                    }
                    else
                    {
                        for (int y = top; y < height - bottom; ++y)
                        {
                            memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                            memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                        }
                    }

                    for (int y = 0; y < top; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * top, static_cast<int64_t>(stride) * size);

                    for (int y = height - bottom; y < height; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (static_cast<int64_t>(height) - bottom - 1), static_cast<int64_t>(stride) * size);
                    break;
                case 2:
                    for (int y = top; y < height - bottom; ++y)
                    {
                        for (int x = 0; x < left; ++x)
                            dstp[stride * y + x] = dstp[stride * y + left * 2 - 1 - x];

                        for (int x = 0; x < right; ++x)
                            dstp[stride * y + width - right + x] = dstp[stride * y + width - right - 1 - x];
                    }

                    for (int y = 0; y < top; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (top * 2LL - 1 - y), static_cast<int64_t>(stride) * size);

                    for (int y = 0; y < bottom; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - bottom - 1 - y), static_cast<int64_t>(stride) * size);
                    break;
                case 3:
                    for (int y = top; y < height - bottom; ++y)
                    {
                        for (int x = 0; x < left; ++x)
                            dstp[stride * y + x] = dstp[stride * y + left * 2 - x];

                        for (int x = 0; x < right; ++x)
                            dstp[stride * y + width - right + x] = dstp[stride * y + width - right - 2 - x];
                    }

                    for (int y = 0; y < top; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (top * 2LL - y), static_cast<int64_t>(stride) * size);

                    for (int y = 0; y < bottom; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - bottom - 2 - y), static_cast<int64_t>(stride) * size);
                    break;
                case 4:
                    for (int y = top; y < height - bottom; ++y)
                    {
                        for (int x = 0; x < left; ++x)
                            dstp[stride * y + x] = dstp[stride * y + width - right - left + x];

                        for (int x = 0; x < right; ++x)
                            dstp[stride * y + width - right + x] = dstp[stride * y + left + x];
                    }

                    for (int y = 0; y < top; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * (static_cast<int64_t>(height) - bottom - top + y), static_cast<int64_t>(stride) * size);

                    for (int y = 0; y < bottom; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + static_cast<int64_t>(stride) * (top + static_cast<int64_t>(y)), static_cast<int64_t>(stride) * size);
                    break;
                case 5:
                    const int bits = vi.BitsPerComponent();
                    const int pl = (vi.IsRGB()) ? 0 : i;

                    for (int y = 0; y < top; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], top - y, top, bits, pl);
                    }

                    const int start_bottom = height - bottom;
                    for (int y = start_bottom; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], y - start_bottom, bottom, bits, pl);
                    }

                    const int start_right = width - right;
                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < left; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], left - x, left, bits, pl);

                        for (int x = 0; x < right; ++x)
                            dstp[stride * y + start_right + x] = lerp<T, T1>(dstp[x], dstp[stride * y + start_right + x], x, right, bits, pl);
                    }
                    break;
            }
        }
        else if (process[i] == 2)
        {
            const int stride = frame->GetPitch(plane);

            env->BitBlt(frame->GetWritePtr(plane), stride, frame->GetReadPtr(plane), stride, frame->GetRowSize(), height);
        }
    }

    return frame;
}

FillBorders::FillBorders(PClip _child, int left, int top, int right, int bottom, int mode, int y, int u, int v, IScriptEnvironment* env)
    : GenericVideoFilter(_child), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_mode(mode)
{
    if (!vi.IsPlanar())
        env->ThrowError("FillBorders: only planar formats are supported.");
    if (left < 0)
        env->ThrowError("FillBorders: left must be equal to or greater than 0.");
    if (top < 0)
        env->ThrowError("FillBorders: top must be equal to or greater than 0.");
    if (right < 0)
        env->ThrowError("FillBorders: right must be equal to or greater than 0.");
    if (bottom < 0)
        env->ThrowError("FillBorders: bottom must be equal to or greater than 0.");
    if (mode < 0 || mode > 5)
        env->ThrowError("FillBorders: invalid mode.");
    if (mode == 0 || mode == 1 || mode == 5)
    {
        if (vi.width < left + right || vi.width <= left || vi.width <= right || vi.width < top + bottom || vi.height <= top || vi.height <= bottom)
            env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
    }
    else if (mode == 2 || mode == 3 || mode == 4)
    {
        if (vi.width < 2 * left || vi.width < 2 * right || vi.height < 2 * top || vi.height < 2 * bottom)
            env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
    }
    if (y < 1 || y > 3)
        env->ThrowError("FillBorders: y must be between 1..3.");
    if (u < 1 || u > 3)
        env->ThrowError("FillBorders: u must be between 1..3.");
    if (v < 1 || v > 3)
        env->ThrowError("FillBorders: v must be between 1..3.");

    has_at_least_v8 = true;
    try { env->CheckVersion(8); }
    catch (const AvisynthError&) { has_at_least_v8 = false; }

    const int planes[3] = { y, u, v };
    const int planecount = min(vi.NumComponents(), 3);
    for (int i = 0; i < planecount; ++i)
    {
        if (vi.IsRGB())
            process[i] = 3;
        else
        {
            switch (planes[i])
            {
                case 3: process[i] = 3; break;
                case 2: process[i] = 2; break;
                default: process[i] = 1; break;
            }
        }
    }

    switch (vi.ComponentSize())
    {
        case 1: processing = &FillBorders::fill<uint8_t, int>; break;
        case 2: processing = &FillBorders::fill<uint16_t, int>; break;
        case 4: processing = &FillBorders::fill<float, float>; break;
    }
}

PVideoFrame __stdcall FillBorders::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame frame = child->GetFrame(n, env);

    if (has_at_least_v8)
    {
        const AVSMap* props = env->getFramePropsRO(frame);
        if (env->propNumElements(props, "_FieldBased") > 0 && env->propGetInt(props, "_FieldBased", 0, nullptr) > 0)
            env->ThrowError("FillBorders: frame must be not interlaced.");
    }

    return (this->*processing)(frame, env);
}

AVSValue __cdecl Create_FillBorders(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    return new FillBorders(
        args[0].AsClip(),
        args[1].AsInt(0),
        args[2].AsInt(0),
        args[3].AsInt(0),
        args[4].AsInt(0),
        args[5].AsInt(0),
        args[6].AsInt(3),
        args[7].AsInt(3),
        args[8].AsInt(3),
        env);
}

class Arguments
{
    AVSValue _args[9];
    const char* _arg_names[9];
    int _idx;

public:
    Arguments() : _args{}, _arg_names{}, _idx{} {}

    void add(AVSValue arg, const char* arg_name = nullptr)
    {
        _args[_idx] = arg;
        _arg_names[_idx] = arg_name;
        ++_idx;
    }

    AVSValue args() const { return{ _args, _idx }; }

    const char* const* arg_names() const { return _arg_names; }
};

static void margins(const AVSValue& args, Arguments* out_args, int mode = 1)
{
    out_args->add(args[0]);

    if (args[mode + 0].Defined())
        out_args->add(args[mode + 0], "left");
    if (args[mode + 1].Defined())
        out_args->add(args[mode + 1], "top");
    if (args[mode + 2].Defined())
        out_args->add(args[mode + 2], "right");
    if (args[mode + 3].Defined())
        out_args->add(args[mode + 3], "bottom");
    if (args[mode + 0].Defined())
        out_args->add(args[mode + 4], "y");
    if (args[mode + 1].Defined())
        out_args->add(args[mode + 5], "u");
    if (args[mode + 1].Defined())
        out_args->add(args[mode + 6], "v");
}

AVSValue __cdecl Create_FillMargins(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    Arguments mapped_args;

    margins(args, &mapped_args);

    return env->Invoke("FillBorders", mapped_args.args(), mapped_args.arg_names()).AsClip();
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("FillBorders", "c[left]i[top]i[right]i[bottom]i[mode]i[y]i[u]i[v]i", Create_FillBorders, 0);
    env->AddFunction("FillMargins", "c[left]i[top]i[right]i[bottom]i[y]i[u]i[v]i", Create_FillMargins, 0);
    return "FillBorders";
}
