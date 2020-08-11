#include <memory>

#include "avisynth.h"
#include "avs/minmax.h"

template <typename T>
static inline void memset16(void* ptr, T value, size_t num)
{
    T* tptr = reinterpret_cast<T*>(ptr);

    while (num-- > 0)
        *tptr++ = value;
}

template <typename T>
static void fillBorders(uint8_t* dstp_, int width, int height, int stride, int left, int top, int right, int bottom, int mode)
{
    T* dstp = reinterpret_cast<T*>(dstp_);

    if constexpr ((std::is_same_v<T, uint8_t>))
    {
        if (mode == 0)
        {
            for (int y = top; y < height - bottom; y++)
            {
                memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                memset(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
            }

            for (int y = top - 1; y >= 0; y--)
            {
                // copy first pixel
                // copy last eight pixels
                dstp[stride * y] = dstp[stride * (y + 1)];
                memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8);

                // weighted average for the rest
                for (int x = 1; x < width - 8; x++)
                {
                    T prev = dstp[stride * (y + 1) + x - 1];
                    T cur = dstp[stride * (y + 1) + x];
                    T next = dstp[stride * (y + 1) + x + 1];
                    dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                }
            }

            for (int y = height - bottom; y < height; y++)
            {
                // copy first pixel
                // copy last eight pixels
                dstp[stride * y] = dstp[stride * (y - 1)];
                memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8);

                // weighted average for the rest
                for (int x = 1; x < width - 8; x++)
                {
                    T prev = dstp[stride * (y - 1) + x - 1];
                    T cur = dstp[stride * (y - 1) + x];
                    T next = dstp[stride * (y - 1) + x + 1];
                    dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                }
            }
        }
        else if (mode == 1)
        {
            for (int y = top; y < height - bottom; y++)
            {
                memset(dstp + static_cast<int64_t>(stride) * y, (dstp + static_cast<int64_t>(stride) * y)[left], left);
                memset(dstp + static_cast<int64_t>(stride) * y + width - right, (dstp + static_cast<int64_t>(stride) * y + width - right)[-1], right);
            }

            for (int y = 0; y < top; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * top, stride);
            }

            for (int y = height - bottom; y < height; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (static_cast<int64_t>(height) - bottom - 1), stride);
            }
        }
        else if (mode == 2)
        {
            for (int y = top; y < height - bottom; y++)
            {
                for (int x = 0; x < left; x++)
                {
                    dstp[stride * y + x] = dstp[stride * y + left * 2 - 1 - x];
                }

                for (int x = 0; x < right; x++)
                {
                    dstp[stride * y + width - right + x] = dstp[stride * y + width - right - 1 - x];
                }
            }

            for (int y = 0; y < top; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (top * static_cast<int64_t>(2) - 1 - y), stride);
            }

            for (int y = 0; y < bottom; y++)
            {
                memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - bottom - 1 - y), stride);
            }
        }
    }
    else
    {
        const int size = sizeof(T);
        width /= size;
        stride /= size;

        if (mode == 0)
        {
            if constexpr ((std::is_same_v<T, uint16_t>))
            {
                for (int y = top; y < height - bottom; y++)
                {
                    memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                    memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                }

                for (int y = top - 1; y >= 0; y--)
                {
                    // copy first pixel
                    // copy last eight pixels
                    dstp[stride * y] = dstp[stride * (y + 1)];
                    memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8 * size);

                    // weighted average for the rest

                    for (int x = 1; x < width - 8; x++)
                    {
                        T prev = dstp[stride * (y + 1) + x - 1];
                        T cur = dstp[stride * (y + 1) + x];
                        T next = dstp[stride * (y + 1) + x + 1];
                        dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                    }
                }

                for (int y = height - bottom; y < height; y++)
                {
                    // copy first pixel
                    // copy last eight pixels
                    dstp[stride * y] = dstp[stride * (y - 1)];
                    memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8 * size);

                    // weighted average for the rest
                    for (int x = 1; x < width - 8; x++)
                    {
                        T prev = dstp[stride * (y - 1) + x - 1];
                        T cur = dstp[stride * (y - 1) + x];
                        T next = dstp[stride * (y - 1) + x + 1];
                        dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                    }
                }
            }
            else
            {
                for (int y = top; y < height - bottom; y++)
                {
                    memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
                    memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
                }

                for (int y = top - 1; y >= 0; y--)
                {
                    // copy first pixel
                    // copy last eight pixels
                    dstp[stride * y] = dstp[stride * (y + 1)];
                    memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8 * size);

                    // weighted average for the rest

                    for (int x = 1; x < width - 8; x++)
                    {
                        T prev = dstp[stride * (y + 1) + x - 1];
                        T cur = dstp[stride * (y + 1) + x];
                        T next = dstp[stride * (y + 1) + x + 1];
                        dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4 / 65536.f) / 8;
                    }
                }

                for (int y = height - bottom; y < height; y++)
                {
                    // copy first pixel
                    // copy last eight pixels
                    dstp[stride * y] = dstp[stride * (y - 1)];
                    memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8 * size);

                    // weighted average for the rest
                    for (int x = 1; x < width - 8; x++)
                    {
                        T prev = dstp[stride * (y - 1) + x - 1];
                        T cur = dstp[stride * (y - 1) + x];
                        T next = dstp[stride * (y - 1) + x + 1];
                        dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4 / 65536.f) / 8;
                    }
                }
            }
        }
        else if (mode == 1)
        {
            for (int y = top; y < height - bottom; y++)
            {
                memset16<T>(dstp + static_cast<int64_t>(stride) * y, (dstp + static_cast<int64_t>(stride) * y)[left], left);
                memset16<T>(dstp + static_cast<int64_t>(stride) * y + width - right, (dstp + static_cast<int64_t>(stride) * y + width - right)[-1], right);
            }

            for (int y = 0; y < top; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * top, static_cast<int64_t>(stride) * size);
            }

            for (int y = height - bottom; y < height; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (static_cast<int64_t>(height) - bottom - 1), static_cast<int64_t>(stride) * size);
            }
        }
        else if (mode == 2)
        {
            for (int y = top; y < height - bottom; y++)
            {
                for (int x = 0; x < left; x++)
                {
                    dstp[stride * y + x] = dstp[stride * y + left * 2 - 1 - x];
                }

                for (int x = 0; x < right; x++)
                {
                    dstp[stride * y + width - right + x] = dstp[stride * y + width - right - 1 - x];
                }
            }

            for (int y = 0; y < top; y++)
            {
                memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (top * static_cast<int64_t>(2) - 1 - y), static_cast<int64_t>(stride) * size);
            }

            for (int y = 0; y < bottom; y++)
            {
                memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - bottom - 1 - y), static_cast<int64_t>(stride) * size);
            }
        }
    }
}

class FillBorders : public GenericVideoFilter
{
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
    int m_mode;
    int m_y;
    int m_u;
    int m_v;
    int process[3];

    void (*fill)(uint8_t*, int, int, int, int, int, int, int, int);

public:
    FillBorders(PClip _child, int left, int top, int right, int bottom, int mode, int y, int u, int v, IScriptEnvironment* env)
        : GenericVideoFilter(_child), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_mode(mode), m_y(y), m_u(u), m_v(v)
    {
        if (left < 0)
            env->ThrowError("FillBorders: left must be equal to or greater than 0");
        if (top < 0)
            env->ThrowError("FillBorders: top must be equal to or greater than 0");
        if (right < 0)
            env->ThrowError("FillBorders: right must be equal to or greater than 0");
        if (bottom < 0)
            env->ThrowError("FillBorders: bottom must be equal to or greater than 0");
        if (mode < 0 || mode > 2)
            env->ThrowError("FillBorders: invalid mode. Valid values are '0 for fillmargins', '1 for repeat', and '2 for mirror'.");
        if (mode == 0 || mode == 1)
        {
            if (vi.width < left + right || vi.width <= left || vi.width <= right || vi.width < top + bottom || vi.height <= top || vi.height <= bottom)
                env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
        }
        else if (mode == 2)
        {
            if (vi.width < 2 * left || vi.width < 2 * right || vi.height < 2 * top || vi.height < 2 * bottom)
                env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
        }
        if (y < 1 || y > 3)
            env->ThrowError("FillBorders: y must be between 1..3");
        if (u < 1 || u > 3)
            env->ThrowError("FillBorders: u must be between 1..3");
        if (v < 1 || v > 3)
            env->ThrowError("FillBorders: v must be between 1..3");

        *process = 0;

        const int planecount = min(vi.NumComponents(), 3);
        for (int i = 0; i < planecount; i++)
        {
            if (vi.IsRGB())
                process[i] = 3;
            else
            {
                switch (i)
                {
                    case 0:
                        switch (m_y)
                        {
                            case 3: process[i] = 3; break;
                            case 2: process[i] = 2; break;
                            default: process[i] = 1; break;
                        }
                        break;
                    case 1:
                        switch (m_u)
                        {
                            case 3: process[i] = 3; break;
                            case 2: process[i] = 2; break;
                            default: process[i] = 1; break;
                        }
                        break;
                    default:
                        switch (m_v)
                        {
                            case 3: process[i] = 3; break;
                            case 2: process[i] = 2; break;
                            default: process[i] = 1; break;
                        }
                        break;
                }
            }
        }

        switch (vi.ComponentSize())
        {
            case 1: fill = fillBorders<uint8_t>; break;
            case 2: fill = fillBorders<uint16_t>; break;
            default: fill = fillBorders<float>; break;
        }
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
        PVideoFrame frame = child->GetFrame(n, env);
        env->MakeWritable(&frame);

        int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
        int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
        int* planes = vi.IsRGB() ? planes_r : planes_y;
        const int planecount = min(vi.NumComponents(), 3);
        for (int pid = 0; pid < planecount; pid++)
        {
            int plane = planes[pid];

            int stride = frame->GetPitch(plane);
            int height = frame->GetHeight(plane);
            int width = frame->GetRowSize(plane);
            uint8_t* dstp = frame->GetWritePtr(plane);

            if (process[pid] == 3)
                 fill(dstp, width, height, stride, m_left >> vi.GetPlaneWidthSubsampling(plane), m_top >> vi.GetPlaneHeightSubsampling(plane), m_right >> vi.GetPlaneWidthSubsampling(plane), m_bottom >> vi.GetPlaneHeightSubsampling(plane), m_mode);
            else if (process[pid] == 2)
                env->BitBlt(dstp, stride, dstp, stride, width, height);
        }

        return frame;
    }

    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
};

AVSValue __cdecl Create_FillBorders(AVSValue args, void *user_data, IScriptEnvironment *env)
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

const AVS_Linkage *AVS_linkage;

extern "C" __declspec(dllexport)
const char * __stdcall AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("FillBorders", "c[left]i[top]i[right]i[bottom]i[mode]i[y]i[u]i[v]i", Create_FillBorders, 0);
    env->AddFunction("FillMargins", "c[left]i[top]i[right]i[bottom]i[y]i[u]i[v]i", Create_FillMargins, 0);
    return "FillBorders";
}
