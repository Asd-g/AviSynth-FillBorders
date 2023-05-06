#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "include\avisynth.h"

#define TS_KERNELSIZE 5 // odd number
#define MAX_TSIZE 10 // even number - maximum supported total size of transient

template <typename T>
AVS_FORCEINLINE void memset16(T* ptr, T value, size_t num)
{
    while (num-- > 0)
        *ptr++ = value;
}

template<typename T, typename T1>
AVS_FORCEINLINE auto lerp(const T1 fill, const T1 src, const int pos, const int size, const int bits, const int plane)
{
    if constexpr (std::is_same_v<T, uint8_t>)
        return std::clamp(((fill * 256 * pos / size) + (src * 256 * (size - pos) / size)) >> 8, 0, 255);
    else if constexpr (std::is_same_v<T, uint16_t>)
    {
        const int64_t max_range{ 1LL << bits };
        return static_cast<int>(std::clamp(((fill * max_range * pos / size) + (src * max_range * (static_cast<int64_t>(size) - pos) / size)) >> bits, static_cast<int64_t>(0), max_range - 1));
    }
    else
        return std::clamp(((fill * pos / size) + (src * (size - pos) / size)), plane ? -0.5f : 0.0f, plane ? 0.5f : 1.0f);
}

class FillBorders : public GenericVideoFilter
{
    std::array<int, 4> m_left;
    std::array<int, 4> m_top;
    std::array<int, 4> m_right;
    std::array<int, 4> m_bottom;
    int m_mode;
    std::array<int, 4> process;
    bool has_at_least_v8;
	int trsize;
	float ts_kernel[TS_KERNELSIZE];

    template<typename T, typename T1>
    PVideoFrame fill(PVideoFrame frame, IScriptEnvironment* env);

    PVideoFrame(FillBorders::* processing)(PVideoFrame frame, IScriptEnvironment* env);

public:
    FillBorders(PClip _child, AVSValue left, AVSValue top, AVSValue right, AVSValue bottom, int mode, int y, int u, int v, int a, int ts, IScriptEnvironment* env);
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
    constexpr std::array<int, 4> planes_y{ PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    constexpr std::array<int, 4> planes_r{ PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    const int* planes{ vi.IsRGB() ? planes_r.data() : planes_y.data() };

	T temp_buf[MAX_TSIZE];

    for (int i{ 0 }; i < vi.NumComponents(); ++i)
    {
        const int height{ frame->GetHeight(planes[i]) };

        if (process[i] == 3)
        {
            const size_t stride{ frame->GetPitch(planes[i]) / sizeof(T) };
            const size_t width{ frame->GetRowSize(planes[i]) / sizeof(T) };
            T* dstp{ reinterpret_cast<T*>(frame->GetWritePtr(planes[i])) };

            switch (m_mode)
            {
                case 0:
                    if constexpr ((std::is_same_v<T, uint8_t>))
                    {
                        for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                        {
                            memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[m_left[i]], m_left[i]);
                            memset(dstp + stride * static_cast<int64_t>(y) + width - m_right[i], (dstp + stride * static_cast<int64_t>(y) + width - m_right[i])[-1], m_right[i]);
                        }
                    }
                    else
                    {
                        for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                        {
                            memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[m_left[i]], m_left[i]);
                            memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - m_right[i], (dstp + stride * static_cast<int64_t>(y) + width - m_right[i])[-1], m_right[i]);
                        }
                    }

                    for (int y{ m_top[i] - 1 }; y >= 0; --y)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y + 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8LL * sizeof(T));

                        // weighted average for the rest
                        for (int x{ 1 }; x < width - 8; ++x)
                        {
                            T prev{ dstp[stride * (y + 1) + x - 1] };
                            T cur{ dstp[stride * (y + 1) + x] };
                            T next{ dstp[stride * (y + 1) + x + 1] };

                            if constexpr (std::is_integral_v<T>)
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
                            else
                                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4 / 2147483647.f) / 8;
                        }
                    }

                    for (int y{ height - m_bottom[i] }; y < height; ++y)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y - 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8LL * sizeof(T));

                        // weighted average for the rest
                        for (int x{ 1 }; x < width - 8; ++x)
                        {
                            T prev{ dstp[stride * (y - 1) + x - 1] };
                            T cur{ dstp[stride * (y - 1) + x] };
                            T next{ dstp[stride * (y - 1) + x + 1] };

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
                        for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                        {
                            memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[m_left[i]], m_left[i]);
                            memset(dstp + stride * static_cast<int64_t>(y) + width - m_right[i], (dstp + stride * static_cast<int64_t>(y) + width - m_right[i])[-1], m_right[i]);
                        }
                    }
                    else
                    {
                        for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                        {
                            memset16<T>(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[m_left[i]], m_left[i]);
                            memset16<T>(dstp + stride * static_cast<int64_t>(y) + width - m_right[i], (dstp + stride * static_cast<int64_t>(y) + width - m_right[i])[-1], m_right[i]);
                        }
                    }

                    for (int y{ 0 }; y < m_top[i]; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * m_top[i], static_cast<int64_t>(stride) * sizeof(T));

                    for (int y{ height - m_bottom[i] }; y < height; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] - 1), static_cast<int64_t>(stride) * sizeof(T));
                    break;
                case 2:
                    for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                    {
                        for (int x{ 0 }; x < m_left[i]; ++x)
                            dstp[stride * y + x] = dstp[stride * y + m_left[i] * 2 - 1 - x];

                        for (int x{ 0 }; x < m_right[i]; ++x)
                            dstp[stride * y + width - m_right[i] + x] = dstp[stride * y + width - m_right[i] - 1 - x];
                    }

                    for (int y{ 0 }; y < m_top[i]; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (m_top[i] * 2LL - 1 - y), static_cast<int64_t>(stride) * sizeof(T));

                    for (int y{ 0 }; y < m_bottom[i]; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] - 1 - y), static_cast<int64_t>(stride) * sizeof(T));
                    break;
                case 3:
                    for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                    {
                        for (int x{ 0 }; x < m_left[i]; ++x)
                            dstp[stride * y + x] = dstp[stride * y + m_left[i] * 2 - x];

                        for (int x{ 0 }; x < m_right[i]; ++x)
                            dstp[stride * y + width - m_right[i] + x] = dstp[stride * y + width - m_right[i] - 2 - x];
                    }

                    for (int y{ 0 }; y < m_top[i]; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (m_top[i] * 2LL - y), static_cast<int64_t>(stride) * sizeof(T));

                    for (int y{ 0 }; y < m_bottom[i]; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] - 2 - y), static_cast<int64_t>(stride) * sizeof(T));
                    break;
                case 4:
                    for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
                    {
                        for (int x{ 0 }; x < m_left[i]; ++x)
                            dstp[stride * y + x] = dstp[stride * y + width - m_right[i] - m_left[i] + x];

                        for (int x{ 0 }; x < m_right[i]; ++x)
                            dstp[stride * y + width - m_right[i] + x] = dstp[stride * y + m_left[i] + x];
                    }

                    for (int y{ 0 }; y < m_top[i]; ++y)
                        memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * (static_cast<int64_t>(height) - m_bottom[i] - m_top[i] + y), static_cast<int64_t>(stride) * sizeof(T));

                    for (int y{ 0 }; y < m_bottom[i]; ++y)
                        memcpy(dstp + stride * (static_cast<int64_t>(height) - m_bottom[i] + static_cast<int64_t>(y)), dstp + static_cast<int64_t>(stride) * (m_top[i] + static_cast<int64_t>(y)), static_cast<int64_t>(stride) * sizeof(T));


					if (trsize > 0)
					{
						if (m_left[i] > 0)
						{
							for (int y{ m_top[i] }; y < height - m_bottom[i]; ++y)
							{
								T* srcp;
								T sample0;
								bool bleftspecial = false;
								if ((m_left[i] - (trsize + TS_KERNELSIZE / 2)) > 0)
									srcp = dstp + static_cast<int64_t>(stride)* y + static_cast<int64_t>(m_left[i] - trsize);
								else
								{
									srcp = dstp + static_cast<int64_t>(stride)* y;
									sample0 = srcp[0];
									bleftspecial = true;
								}

								for (int xp{ 0 }; xp < trsize * 2; ++xp)
								{
									float sum = 0.0f;
									for (int k{ 0 }; k < TS_KERNELSIZE; ++k)
									{
										if (!bleftspecial)
											sum += (float)srcp[xp + k - TS_KERNELSIZE / 2] * ts_kernel[k];
										else
										{
											T sample;
											if (xp + k + m_left[i] - trsize - (TS_KERNELSIZE / 2) < 0)
												sample = sample0;
											else
												sample = srcp[xp + k + m_left[i] - trsize - TS_KERNELSIZE / 2];

											sum += (float)sample * ts_kernel[k];
										}
									}
									if (sizeof(T) < 4)
										temp_buf[xp] = T(sum + 0.5f);
									else
										temp_buf[xp] = sum;
								}

								//copy back processed buf
								if (!bleftspecial)
									memcpy(srcp, temp_buf, static_cast<int64_t>(trsize * 2) * sizeof(T));
								else
									memcpy(srcp + static_cast<int64_t>(m_left[i] - trsize), temp_buf, static_cast<int64_t>(trsize * 2) * sizeof(T));

								//						for (int x{ 0 }; x < m_left[i]; ++x)
								//							dstp[stride * y + x] = dstp[stride * y + width - m_right[i] - m_left[i] + x];

							}

						}

					}

                    break;
                case 5:
                {
                    const int bits{ vi.BitsPerComponent() };
                    const int pl{ (vi.IsRGB()) ? 0 : i };

                    for (int y{ 0 }; y < m_top[i]; ++y)
                    {
                        for (int x{ 0 }; x < width; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], m_top[i] - y, m_top[i], bits, pl);
                    }

                    const int start_bottom{ height - m_bottom[i] };
                    for (int y{ start_bottom }; y < height; ++y)
                    {
                        for (int x{ 0 }; x < width; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], y - start_bottom, m_bottom[i], bits, pl);
                    }

                    const size_t start_right{ width - m_right[i] };
                    for (int y{ 0 }; y < height; ++y)
                    {
                        for (int x{ 0 }; x < m_left[i]; ++x)
                            dstp[stride * y + x] = lerp<T, T1>(dstp[x], dstp[stride * y + x], m_left[i] - x, m_left[i], bits, pl);

                        for (int x{ 0 }; x < m_right[i]; ++x)
                            dstp[stride * y + start_right + x] = lerp<T, T1>(dstp[x], dstp[stride * y + start_right + x], x, m_right[i], bits, pl);
                    }
                    break;
                }
                case 6:
                    for (int x{ m_left[i] - 1 }; x >= 0; --x)
                    {
                        // copy pixels until m_top[i] + 3/m_bottom[i] + 3
                        // this way we avoid darkened corners when all sides need filling
                        for (int y{ 0 }; y < m_top[i] + 3; ++y)
                            dstp[stride * y + x] = dstp[stride * y + x + 1];
                        for (int y{ m_bottom[i] + 3 }; y > 0; --y)
                            dstp[stride * (height - y) + x] = dstp[stride * (height - y) + x + 1];

                        // weighted average for the rest
                        for (int y{ m_top[i] + 3 }; y < height - (m_bottom[i] + 3); ++y)
                        {
                            T prev{ dstp[stride * (y - 1) + x + 1] };
                            T cur{ dstp[stride * (y)+x + 1] };
                            T next{ dstp[stride * (y + 1) + x + 1] };

                            T ref_prev{ dstp[stride * (y - 1) + x + 2] };
                            T ref_cur{ dstp[stride * (y)+x + 2] };
                            T ref_next{ dstp[stride * (y + 1) + x + 2] };

                            T fill_prev, fill_cur, fill_next;

                            if constexpr (std::is_integral_v<T>)
                            {
                                fill_prev = llrint((5LL * prev + 3LL * cur + 1 * next) / 9.0);
                                fill_cur = llrint((1 * prev + 3LL * cur + 1 * next) / 5.0);
                                fill_next = llrint((1 * prev + 3LL * cur + 5LL * next) / 9.0);
                            }
                            else
                            {
                                fill_prev = (5 * prev + 3 * cur + 1 * next) / 9.0f;
                                fill_cur = (1 * prev + 3 * cur + 1 * next) / 5.0f;
                                fill_next = (1 * prev + 3 * cur + 5 * next) / 9.0f;
                            }

                            T blur_prev{ static_cast<T>((2 * ref_prev + ref_cur + dstp[stride * (y - 2) + x + 2]) / 4) };
                            T blur_next{ static_cast<T>((2 * ref_next + ref_cur + dstp[stride * (y + 2) + x + 2]) / 4) };

                            T diff_next{ static_cast<T>(abs(ref_next - fill_cur)) };
                            T diff_prev{ static_cast<T>(abs(ref_prev - fill_cur)) };
                            T thr_next{ static_cast<T>(abs(ref_next - blur_next)) };
                            T thr_prev{ static_cast<T>(abs(ref_prev - blur_prev)) };

                            if (diff_next > thr_next)
                            {
                                if (diff_prev < diff_next)
                                    dstp[stride * y + x] = fill_prev;
                                else
                                    dstp[stride * y + x] = fill_next;
                            }
                            else if (diff_prev > thr_prev)
                                dstp[stride * y + x] = fill_next;
                            else
                                dstp[stride * y + x] = fill_cur;
                        }
                    }

                    for (size_t x{ width - m_right[i] }; x < width; ++x)
                    {
                        // copy pixels until m_top[i] + 3/m_bottom[i] + 3
                        // this way we avoid darkened corners when all sides need filling
                        for (int y{ 0 }; y < m_top[i] + 3; ++y)
                            dstp[stride * y + x] = dstp[stride * y + x - 1];
                        for (int y{ m_bottom[i] + 3 }; y > 0; --y)
                            dstp[stride * (height - y) + x] = dstp[stride * (height - y) + x - 1];

                        // weighted average for the rest
                        for (int y{ m_top[i] + 3 }; y < height - (m_bottom[i] + 3); ++y)
                        {
                            T prev{ dstp[stride * (y - 1) + x - 1] };
                            T cur{ dstp[stride * (y)+x - 1] };
                            T next{ dstp[stride * (y + 1) + x - 1] };

                            T ref_prev{ dstp[stride * (y - 1) + x - 2] };
                            T ref_cur{ dstp[stride * (y)+x - 2] };
                            T ref_next{ dstp[stride * (y + 1) + x - 2] };

                            T fill_prev, fill_cur, fill_next;

                            if constexpr (std::is_integral_v<T>)
                            {
                                fill_prev = llrint((5LL * prev + 3LL * cur + 1 * next) / 9.0);
                                fill_cur = llrint((1 * prev + 3LL * cur + 1 * next) / 5.0);
                                fill_next = llrint((1 * prev + 3LL * cur + 5LL * next) / 9.0);
                            }
                            else
                            {
                                fill_prev = (5 * prev + 3 * cur + 1 * next) / 9.0f;
                                fill_cur = (1 * prev + 3 * cur + 1 * next) / 5.0f;
                                fill_next = (1 * prev + 3 * cur + 5 * next) / 9.0f;
                            }

                            T blur_prev{ static_cast<T>((2 * ref_prev + ref_cur + dstp[stride * (y - 2) + x - 2]) / 4) };
                            T blur_next{ static_cast<T>((2 * ref_next + ref_cur + dstp[stride * (y + 2) + x - 2]) / 4) };

                            T diff_next{ static_cast<T>(abs(ref_next - fill_cur)) };
                            T diff_prev{ static_cast<T>(abs(ref_prev - fill_cur)) };
                            T thr_next{ static_cast<T>(abs(ref_next - blur_next)) };
                            T thr_prev{ static_cast<T>(abs(ref_prev - blur_prev)) };

                            if (diff_next > thr_next)
                            {
                                if (diff_prev < diff_next)
                                    dstp[stride * y + x] = fill_prev;
                                else
                                    dstp[stride * y + x] = fill_next;
                            }
                            else if (diff_prev > thr_prev)
                                dstp[stride * y + x] = fill_next;
                            else
                                dstp[stride * y + x] = fill_cur;
                        }
                    }

                    for (int y{ m_top[i] - 1 }; y >= 0; --y)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y + 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (y + 1LL) + width - 8, 8 * sizeof(T));

                        // weighted average for the rest
                        for (int x{ 1 }; x < width - 8; ++x)
                        {
                            T prev{ dstp[stride * (y + 1) + x - 1] };
                            T cur{ dstp[stride * (y + 1) + x] };
                            T next{ dstp[stride * (y + 1) + x + 1] };

                            T ref_prev{ dstp[stride * (y + 2) + x - 1] };
                            T ref_cur{ dstp[stride * (y + 2) + x] };
                            T ref_next{ dstp[stride * (y + 2) + x + 1] };

                            T fill_prev, fill_cur, fill_next;

                            if constexpr (std::is_integral_v<T>)
                            {
                                fill_prev = llrint((5LL * prev + 3LL * cur + 1 * next) / 9.0);
                                fill_cur = llrint((1 * prev + 3LL * cur + 1 * next) / 5.0);
                                fill_next = llrint((1 * prev + 3LL * cur + 5LL * next) / 9.0);
                            }
                            else
                            {
                                fill_prev = (5 * prev + 3 * cur + 1 * next) / 9.0f;
                                fill_cur = (1 * prev + 3 * cur + 1 * next) / 5.0f;
                                fill_next = (1 * prev + 3 * cur + 5 * next) / 9.0f;
                            }

                            T blur_prev{ static_cast<T>((2 * ref_prev + ref_cur + dstp[stride * (y + 2) + x - 2]) / 4) };
                            T blur_next{ static_cast<T>((2 * ref_next + ref_cur + dstp[stride * (y + 2) + x + 2]) / 4) };

                            T diff_next{ static_cast<T>(abs(ref_next - fill_cur)) };
                            T diff_prev{ static_cast<T>(abs(ref_prev - fill_cur)) };
                            T thr_next{ static_cast<T>(abs(ref_next - blur_next)) };
                            T thr_prev{ static_cast<T>(abs(ref_prev - blur_prev)) };

                            if (diff_next > thr_next)
                            {
                                if (diff_prev < diff_next)
                                    dstp[stride * y + x] = fill_prev;
                                else
                                    dstp[stride * y + x] = fill_next;
                            }
                            else if (diff_prev > thr_prev)
                                dstp[stride * y + x] = fill_next;
                            else
                                dstp[stride * y + x] = fill_cur;
                        }
                    }

                    for (int y{ height - m_bottom[i] }; y < height; ++y)
                    {
                        // copy first pixel
                        // copy last eight pixels
                        dstp[stride * y] = dstp[stride * (y - 1)];
                        memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (y - 1LL) + width - 8, 8 * sizeof(T));

                        // weighted average for the rest
                        for (int x{ 1 }; x < width - 8; ++x)
                        {
                            T prev{ dstp[stride * (y - 1) + x - 1] };
                            T cur{ dstp[stride * (y - 1) + x] };
                            T next{ dstp[stride * (y - 1) + x + 1] };

                            T ref_prev{ dstp[stride * (y - 2) + x - 1] };
                            T ref_cur{ dstp[stride * (y - 2) + x] };
                            T ref_next{ dstp[stride * (y - 2) + x + 1] };

                            T fill_prev, fill_cur, fill_next;

                            if constexpr (std::is_integral_v<T>)
                            {
                                fill_prev = llrint((5LL * prev + 3LL * cur + 1 * next) / 9.0);
                                fill_cur = llrint((1 * prev + 3LL * cur + 1 * next) / 5.0);
                                fill_next = llrint((1 * prev + 3LL * cur + 5LL * next) / 9.0);
                            }
                            else
                            {
                                fill_prev = (5 * prev + 3 * cur + 1 * next) / 9.0f;
                                fill_cur = (1 * prev + 3 * cur + 1 * next) / 5.0f;
                                fill_next = (1 * prev + 3 * cur + 5 * next) / 9.0f;
                            }

                            T blur_prev{ static_cast<T>((2 * ref_prev + ref_cur + dstp[stride * (y - 2) + x - 2]) / 4) };
                            T blur_next{ static_cast<T>((2 * ref_next + ref_cur + dstp[stride * (y - 2) + x + 2]) / 4) };

                            T diff_next{ static_cast<T>(abs(ref_next - fill_cur)) };
                            T diff_prev{ static_cast<T>(abs(ref_prev - fill_cur)) };
                            T thr_next{ static_cast<T>(abs(ref_next - blur_next)) };
                            T thr_prev{ static_cast<T>(abs(ref_prev - blur_prev)) };

                            if (diff_next > thr_next)
                            {
                                if (diff_prev < diff_next)
                                    dstp[stride * y + x] = fill_prev;
                                else
                                    dstp[stride * y + x] = fill_next;
                            }
                            else if (diff_prev > thr_prev)
                                dstp[stride * y + x] = fill_next;
                            else
                                dstp[stride * y + x] = fill_cur;
                        }
                    }
                    break;
            }
        }
        else if (process[i] == 2)
        {
            const int stride{ frame->GetPitch(planes[i]) };

            env->BitBlt(frame->GetWritePtr(planes[i]), stride, frame->GetReadPtr(planes[i]), stride, frame->GetRowSize(planes[i]), height);
        }
    }

    return frame;
}

FillBorders::FillBorders(PClip _child, AVSValue left, AVSValue top, AVSValue right, AVSValue bottom, int mode, int y, int u, int v, int a, int ts, IScriptEnvironment* env)
    : GenericVideoFilter(_child), m_mode(mode), process{ 1, 1, 1, 1 }
{
    if (!vi.IsPlanar())
        env->ThrowError("FillBorders: only planar formats are supported.");

    const int sw{ (vi.IsY() || vi.IsRGB()) ? 0 : vi.GetPlaneWidthSubsampling(PLANAR_U) };
    const int sh{ (vi.IsY() || vi.IsRGB()) ? 0 : vi.GetPlaneHeightSubsampling(PLANAR_U) };

    const int num_left{ (left.Defined() ? left.ArraySize() : 0) };
    if (num_left > vi.NumComponents())
        env->ThrowError("FillBorders: more left given than there are planes");

	for (int i{ 0 }; i < num_left; ++i)
	{
		m_left[i] = left[i].AsInt();
		if (ts > m_left[i]) env->ThrowError("FillBorders: ts must be lower or equal to fill size");
	}

	trsize = ts;

    switch (num_left)
    {
        case 0: m_left.fill(0); break;
        case 1:
        {
            m_left[1] = m_left[2] = m_left[0] >> sw;
            m_left[3] = m_left[0];
            break;
        }
        case 2:
        {
            m_left[2] = m_left[1];
            m_left[3] = m_left[0];
            break;
        }
        case 3: m_left[3] = m_left[0]; break;
    }

    const int num_top{ (top.Defined() ? top.ArraySize() : 0) };
    if (num_top > vi.NumComponents())
        env->ThrowError("FillBorders: more top given than there are planes");

    for (int i{ 0 }; i < num_top; ++i)
        m_top[i] = top[i].AsInt();

    switch (num_top)
    {
        case 0: m_top.fill(0); break;
        case 1:
        {
            m_top[1] = m_top[2] = m_top[0] >> sh;
            m_top[3] = m_top[0];
            break;
        }
        case 2:
        {
            m_top[2] = m_top[1];
            m_top[3] = m_top[0];
            break;
        }
        case 3: m_top[3] = m_top[0]; break;
    }

    const int num_right{ (right.Defined() ? right.ArraySize() : 0) };
    if (num_right > vi.NumComponents())
        env->ThrowError("FillBorders: more right given than there are planes");

    for (int i{ 0 }; i < num_right; ++i)
        m_right[i] = right[i].AsInt();

    switch (num_right)
    {
        case 0: m_right.fill(0); break;
        case 1:
        {
            m_right[1] = m_right[2] = m_right[0] >> sw;
            m_right[3] = m_right[0];
            break;
        }
        case 2:
        {
            m_right[2] = m_right[1];
            m_right[3] = m_right[0];
            break;
        }
        case 3: m_right[3] = m_right[0]; break;
    }

    const int num_bottom{ (bottom.Defined() ? bottom.ArraySize() : 0) };
    if (num_bottom > vi.NumComponents())
        env->ThrowError("FillBorders: more bottom given than there are planes");

    for (int i{ 0 }; i < num_bottom; ++i)
        m_bottom[i] = bottom[i].AsInt();

    switch (num_bottom)
    {
        case 0: m_bottom.fill(0); break;
        case 1:
        {
            m_bottom[1] = m_bottom[2] = m_bottom[0] >> sh;
            m_bottom[3] = m_bottom[0];
            break;
        }
        case 2:
        {
            m_bottom[2] = m_bottom[1];
            m_bottom[3] = m_bottom[0];
            break;
        }
        case 3: m_bottom[3] = m_bottom[0]; break;
    }

    if (y < 1 || y > 3)
        env->ThrowError("FillBorders: y must be between 1..3.");
    if (u < 1 || u > 3)
        env->ThrowError("FillBorders: u must be between 1..3.");
    if (v < 1 || v > 3)
        env->ThrowError("FillBorders: v must be between 1..3.");
    if (a < 1 || a > 3)
        env->ThrowError("FillBorders: a must be between 1..3.");

    const int chr_w{ vi.width >> sw };
    const int chr_h{ vi.height >> sh };
    const std::array<int, 4> w{ vi.width, chr_w, chr_w, vi.width };
    const std::array<int, 4> h{ vi.height, chr_h, chr_h, vi.height };

    has_at_least_v8 = env->FunctionExists("propShow");

    const std::array<int, 4> planes{ y, u, v, a };
    for (int i{ 0 }; i < vi.NumComponents(); ++i)
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

        if (process[i] == 3)
        {
            if (m_left[i] < 0)
                env->ThrowError("FillBorders: left must be equal to or greater than 0.");
            if (m_top[i] < 0)
                env->ThrowError("FillBorders: top must be equal to or greater than 0.");
            if (m_right[i] < 0)
                env->ThrowError("FillBorders: right must be equal to or greater than 0.");
            if (m_bottom[i] < 0)
                env->ThrowError("FillBorders: bottom must be equal to or greater than 0.");
            if (m_mode == 0 || m_mode == 1 || m_mode == 5 || m_mode == 6)
            {
                if (w[i] < m_left[i] + m_right[i] || w[i] <= m_left[i] || w[i] <= m_right[i] || h[i] < m_top[i] + m_bottom[i] || h[i] <= m_top[i] || h[i] <= m_bottom[i])
                    env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
            }
            else if (m_mode == 2 || m_mode == 3 || m_mode == 4)
            {
                if (w[i] < 2 * m_left[i] || w[i] < 2 * m_right[i] || h[i] < 2 * m_top[i] || h[i] < 2 * m_bottom[i])
                    env->ThrowError("FillBorders: the input clip is too small or the borders are too big.");
            }
        }
    }

    switch (vi.ComponentSize())
    {
        case 1: 
			processing = &FillBorders::fill<uint8_t, int>; 
			break;
        case 2: 
			processing = &FillBorders::fill<uint16_t, int>;
			break;
        default: 
			processing = &FillBorders::fill<float, float>; 
			break;
    }

	if (trsize != 0)
	{
		// make simple gauss kernel at first
		float p = 1.2f; // make it ts_kernel param (1)
		for (int i = 0; i < TS_KERNELSIZE; i++)
		{
			int val = i - TS_KERNELSIZE / 2;
			ts_kernel[i] = (float)pow(2.0, -p * val * val);
		}

		// normalize
		float sum = 0;
		for (int i = 0; i < TS_KERNELSIZE; i++)
		{
			sum += ts_kernel[i];
		}

		for (int i = 0; i < TS_KERNELSIZE; i++)
		{
			ts_kernel[i] /= sum;
		}

	}
}

PVideoFrame __stdcall FillBorders::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame frame{ child->GetFrame(n, env) };

    if (has_at_least_v8)
    {
        const AVSMap* props{ env->getFramePropsRO(frame) };
        if (env->propNumElements(props, "_FieldBased") > 0 && env->propGetInt(props, "_FieldBased", 0, nullptr) > 0)
            env->ThrowError("FillBorders: frame must be not interlaced.");
    }

	return (this->*processing)(frame, env);
}

AVSValue __cdecl Create_FillBorders(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    enum { Clip, Left, Top, Right, Bottom, Mode, Y, U, V, A, ts };

    return new FillBorders(
        args[Clip].AsClip(),
        args[Left],
        args[Top],
        args[Right],
        args[Bottom],
        args[Mode].AsInt(0),
        args[Y].AsInt(3),
        args[U].AsInt(3),
        args[V].AsInt(3),
        args[A].AsInt(3),
		args[ts].AsInt(3),
        env);
}

class Arguments
{
    AVSValue _args[10];
    const char* _arg_names[10];
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

void margins(const AVSValue& args, Arguments* out_args, int mode = 1)
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

    env->AddFunction("FillBorders", "c[left]i*[top]i*[right]i*[bottom]i*[mode]i[y]i[u]i[v]i[a]i[ts]i", Create_FillBorders, 0);
    env->AddFunction("FillMargins", "c[left]i[top]i[right]i[bottom]i[y]i[u]i[v]i", Create_FillMargins, 0);
    return "FillBorders";
}
