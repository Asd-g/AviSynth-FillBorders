#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

#include <avisynth.h>

static constexpr int TS_KERNELSIZE{5};
static constexpr int MAX_TSIZE{10};

template<typename T_Pixel>
AVS_FORCEINLINE void memset16(T_Pixel* AVS_RESTRICT ptr, const T_Pixel value, const size_t num) noexcept
{
    for (size_t i{}; i < num; ++i)
        ptr[i] = value;
}

template<typename T_Pixel, typename T_Calc>
AVS_FORCEINLINE auto lerp(const T_Calc fill, const T_Calc src, const int pos, const int size, const int bits, const int plane)
{
    if constexpr (std::is_same_v<T_Pixel, uint8_t>)
        return std::clamp((fill * 256 * pos + src * 256 * (size - pos)) / size >> 8, 0, 255);
    else if constexpr (std::is_same_v<T_Pixel, uint16_t>)
    {
        const int64_t max_range{1LL << bits};
        return static_cast<int>(
            std::clamp((fill * max_range * pos + src * max_range * (static_cast<int64_t>(size) - pos)) / size / max_range,
                static_cast<int64_t>(0), max_range - 1));
    }
    else
        return std::clamp((fill * pos + src * (size - pos)) / size, plane ? -0.5f : 0.0f, plane ? 0.5f : 1.0f);
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL>
class FillBorders : public GenericVideoFilter
{
    const int m_subsample_shift_h;
    const int m_subsample_shift_w;
    const std::array<int, 4> m_left;
    const std::array<int, 4> m_top;
    const std::array<int, 4> m_right;
    const std::array<int, 4> m_bottom;
    const bool m_interlaced;
    const int m_ts_runtime;
    const int m_ts_mode_runtime;
    const std::optional<std::array<T_Calc, 4>> m_fade_target_value;
    const std::array<int, 4> m_process;
    const std::array<float, TS_KERNELSIZE> m_ts_kernel_data;
    const bool has_at_least_v8;

    void handle_mode_0_fillmargins_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;
    void handle_mode_1_repeat_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;
    void handle_mode_2_mirror_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;
    void handle_mode_3_reflect_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;
    void handle_mode_4_wrap_base_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;
    void apply_mode4_transient_smoothing_impl(T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride,
        int component_idx, const int bits, const int lerp_plane_idx_param, T_Pixel* AVS_RESTRICT temp_buf) const noexcept;
    void handle_mode_5_fade_impl(T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx,
        const int bits, const int lerp_plane_idx_param) const noexcept;
    void handle_mode_6_fixborders_impl(
        T_Pixel* AVS_RESTRICT dstp, int plane_width, int plane_height, size_t stride, int component_idx) const noexcept;

    void smooth_lerp_left_impl(T_Pixel* AVS_RESTRICT row_ptr, int plane_width, int border_size, int tr_s, const int bits,
        const int lerp_plane_idx_param) const noexcept;
    void smooth_lerp_right_impl(T_Pixel* AVS_RESTRICT row_ptr, int plane_width, int border_size, int tr_s, const int bits,
        const int lerp_plane_idx_param) const noexcept;
    void smooth_lerp_top_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start, int plane_height, size_t stride, int border_size, int tr_s,
        const int bits, const int lerp_plane_idx_param) const noexcept;
    void smooth_lerp_bottom_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start, int plane_height, size_t stride, int border_size, int tr_s,
        const int bits, const int lerp_plane_idx_param) const noexcept;

    void smooth_gaussian_horizontal_impl(T_Pixel* AVS_RESTRICT row_ptr, int plane_width, int border_size, int tr_s, bool is_left_border,
        T_Pixel* AVS_RESTRICT temp_buf, bool modify_original_pixels) const noexcept;
    void smooth_gaussian_vertical_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start, int plane_height, size_t stride, int border_size,
        int tr_s, bool is_top_border, T_Pixel* AVS_RESTRICT temp_buf, bool modify_original_pixels) const noexcept;

public:
    FillBorders(PClip _child, AVSValue left, AVSValue top, AVSValue right, AVSValue bottom, int y, int u, int v, int a, bool interlaced,
        int ts, int ts_mode, AVSValue fade_value, IScriptEnvironment* env);

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override;

    int __stdcall SetCacheHints(int cachehints, int frame_range) noexcept override
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    }
};

std::array<int, 4> initialize_border_array(const AVSValue& border_avs_val, const VideoInfo& vi_ref, const int subsample_shift,
    IScriptEnvironment* env, const char* border_name_for_error)
{
    std::array<int, 4> result_array{};
    const int num_values_from_script{border_avs_val.Defined() ? border_avs_val.ArraySize() : 0};

    if (!num_values_from_script)
        return {};

    if (num_values_from_script > vi_ref.NumComponents())
        env->ThrowError("FillBorders: more %s values given than there are planes", border_name_for_error);

    for (int i{0}; i < num_values_from_script; ++i)
        result_array[i] = border_avs_val[i].AsInt();

    if (num_values_from_script == 1)
    {
        const int chroma_value{result_array[0] >> subsample_shift};
        result_array[1] = chroma_value;
        result_array[2] = chroma_value;
        result_array[3] = result_array[0];
    }
    else if (num_values_from_script == 2)
    {
        result_array[2] = result_array[1];
        result_array[3] = result_array[0];
    }
    else if (num_values_from_script == 3)
        result_array[3] = result_array[0];

    return result_array;
}

template<typename T_Calc>
std::optional<std::array<T_Calc, 4>> parse_and_scale_fade_targets(
    const AVSValue& fade_value_from_script, const VideoInfo& vi_ref, IScriptEnvironment* env)
{
    if (!fade_value_from_script.Defined() || (fade_value_from_script.ArraySize() == 1 && fade_value_from_script[0].AsInt() == -1))
        return std::nullopt;

    std::array<T_Calc, 4> targets{};

    const int bits{vi_ref.BitsPerComponent()};
    const bool is_float_clip{bits == 32};

    auto is_conceptual_comp_yuv_chroma{[&](int conceptual_comp_idx) -> bool {
        if (vi_ref.IsRGB() || !is_float_clip)
            return false;

        return (conceptual_comp_idx == 1 || conceptual_comp_idx == 2);
    }};

    auto get_single_component_target_value{[&](const AVSValue& val_element, int conceptual_comp_idx) -> T_Calc {
        if (is_float_clip)
        {
            const float float_val{val_element.AsFloatf(0.0f)};

            if (is_conceptual_comp_yuv_chroma(conceptual_comp_idx))
                return static_cast<T_Calc>(std::clamp(float_val, -0.5f, 0.5f));
            else // Luma, Alpha, RGB
                return static_cast<T_Calc>(std::clamp(float_val, 0.0f, 1.0f));
        }
        else
            return static_cast<T_Calc>(std::clamp(val_element.AsInt(0), 0, ((1 << bits) - 1)));
    }};

    const int array_size{fade_value_from_script.ArraySize()};

    bool types_consistent{true};
    bool first_is_int{};

    if (array_size > 0)
    {
        first_is_int = fade_value_from_script[0].IsInt();

        for (int k{1}; k < array_size; ++k)
        {
            if (fade_value_from_script[k].IsInt() != first_is_int)
            {
                types_consistent = false;
                break;
            }
        }
    }

    if (!types_consistent)
        env->ThrowError("FillBorders: 'fade_value' array elements must be all integers or all floats.");

    if (first_is_int && is_float_clip)
        env->ThrowError("FillBorders: 'fade_value' provides integer array, but clip is float type.");

    if (!first_is_int && array_size > 0 && !is_float_clip)
        env->ThrowError("FillBorders: 'fade_value' provides float array, but clip is integer type.");

    const int num_components{vi_ref.NumComponents()};

    if (array_size == 1)
    {
        T_Calc val_for_all{get_single_component_target_value(fade_value_from_script[0], 0)};

        for (int i{0}; i < num_components; ++i)
            targets[i] = get_single_component_target_value(fade_value_from_script[0], i);
    }
    else if (array_size == num_components || (array_size == 4 && num_components == 3))
    {
        for (int i{0}; i < array_size; ++i)
            targets[i] = get_single_component_target_value(fade_value_from_script[i], i);
    }
    else
        env->ThrowError("FillBorders: 'fade_value' array must have 1 element, or %d elements%s.", num_components);

    return std::optional<std::array<T_Calc, 4>>{targets};
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL>
FillBorders<T_Pixel, T_Calc, MODE_VAL>::FillBorders(PClip _child, AVSValue left, AVSValue top, AVSValue right, AVSValue bottom, int y,
    int u, int v, int a, bool interlaced, int ts, int ts_mode, AVSValue fade_value, IScriptEnvironment* env)
    : GenericVideoFilter(_child),
      m_subsample_shift_h((vi.IsY() || vi.IsRGB()) ? 0 : vi.GetPlaneHeightSubsampling(PLANAR_U)),
      m_subsample_shift_w((vi.IsY() || vi.IsRGB()) ? 0 : vi.GetPlaneWidthSubsampling(PLANAR_U)),
      m_left(initialize_border_array(left, vi, m_subsample_shift_w, env, "left")),
      m_top(initialize_border_array(top, vi, m_subsample_shift_h, env, "top")),
      m_right(initialize_border_array(right, vi, m_subsample_shift_w, env, "right")),
      m_bottom(initialize_border_array(bottom, vi, m_subsample_shift_h, env, "bottom")),
      m_interlaced(interlaced),
      m_ts_runtime(ts),
      m_ts_mode_runtime(ts_mode),
      m_process([&] {
          return (vi.IsRGB()) ? std::array<int, 4>{3, 3, 3, (vi.NumComponents() == 4) ? a : 1}
                              : std::array<int, 4>{y, u, v, (vi.NumComponents() == 4) ? a : 1};
      }()),
      m_ts_kernel_data([&] {
          std::array<float, TS_KERNELSIZE> kernel{};

          if (ts > 0 && MODE_VAL == 4 && (ts_mode == 1 || ts_mode == 2))
          {
              const float p_gauss{1.2f};
              float sum{};

              for (int i{0}; i < TS_KERNELSIZE; ++i)
              {
                  const int val{i - TS_KERNELSIZE / 2};
                  kernel[i] = static_cast<float>(std::pow(2.0, -p_gauss * val * val));
                  sum += kernel[i];
              }

              if (sum != 0.0f)
              {
                  for (int i{0}; i < TS_KERNELSIZE; ++i)
                      kernel[i] /= sum;
              }
              else if (TS_KERNELSIZE > 0)
                  kernel[TS_KERNELSIZE / 2] = 1.0f;
          }

          return kernel;
      }()),
      m_fade_target_value(parse_and_scale_fade_targets<T_Calc>(fade_value, vi, env)),
      has_at_least_v8(env->FunctionExists("propShow"))
{
    if (!vi.IsPlanar())
        env->ThrowError("FillBorders: only planar formats are supported.");

    if (y < 1 || y > 3)
        env->ThrowError("FillBorders: y must be between 1..3.");

    if (u < 1 || u > 3)
        env->ThrowError("FillBorders: u must be between 1..3.");

    if (v < 1 || v > 3)
        env->ThrowError("FillBorders: v must be between 1..3.");

    if (a < 1 || a > 3)
        env->ThrowError("FillBorders: a must be between 1..3.");

    if (m_ts_runtime < 0)
        env->ThrowError("FillBorders: ts must be non-negative.");

    if (m_ts_runtime * 2 > MAX_TSIZE && m_ts_runtime > 0)
        env->ThrowError("FillBorders: ts*2 cannot exceed MAX_TSIZE (%d).", MAX_TSIZE);

    if (m_ts_mode_runtime < 0 || m_ts_mode_runtime > 2)
        env->ThrowError("FillBorders: ts_mode must be 0, 1, or 2.");

    const int chr_w{vi.width >> m_subsample_shift_w};
    const int chr_h{vi.height >> m_subsample_shift_h};
    const std::array<int, 4> plane_widths_map{vi.width, chr_w, chr_w, vi.width};
    const std::array<int, 4> plane_heights_map{vi.height, chr_h, chr_h, vi.height};

    for (int i{}; i < vi.NumComponents(); ++i)
    {
        if (m_process[i] == 3)
        {
            if (m_left[i] < 0)
                env->ThrowError("FillBorders: left must be equal to or greater than 0.");

            if (m_top[i] < 0)
                env->ThrowError("FillBorders: top must be equal to or greater than 0.");

            if (m_right[i] < 0)
                env->ThrowError("FillBorders: right must be equal to or greater than 0.");

            if (m_bottom[i] < 0)
                env->ThrowError("FillBorders: bottom must be equal to or greater than 0.");

            if (m_ts_runtime > 0 && MODE_VAL == 4)
            {
                if (m_left[i] > 0 && m_ts_runtime > m_left[i])
                    env->ThrowError("FillBorders: ts must be <= left border size for component %d.", i);

                if (m_top[i] > 0 && m_ts_runtime > m_top[i])
                    env->ThrowError("FillBorders: ts must be <= top border size for component %d.", i);

                if (m_right[i] > 0 && m_ts_runtime > m_right[i])
                    env->ThrowError("FillBorders: ts must be <= right border size for component %d.", i);

                if (m_bottom[i] > 0 && m_ts_runtime > m_bottom[i])
                    env->ThrowError("FillBorders: ts must be <= bottom border size for component %d.", i);
            }

            const int current_plane_w_check{plane_widths_map[i]};
            const int current_plane_h_check{plane_heights_map[i]};

            if (MODE_VAL == 0 || MODE_VAL == 1 || MODE_VAL == 5 || MODE_VAL == 6)
            {
                if (current_plane_w_check < m_left[i] + m_right[i] || current_plane_h_check < m_top[i] + m_bottom[i])
                    env->ThrowError("FillBorders: borders are too big for component %d (mode %d).", i, MODE_VAL);
            }
            else if (MODE_VAL == 2 || MODE_VAL == 3)
            {
                if (m_left[i] > 0 && current_plane_w_check < 2 * m_left[i])
                    env->ThrowError("FillBorders: clip too small for left border on component %d, mode %d", i, MODE_VAL);

                if (m_right[i] > 0 && current_plane_w_check < 2 * m_right[i])
                    env->ThrowError("FillBorders: clip too small for right border on component %d, mode %d", i, MODE_VAL);

                if (m_top[i] > 0 && current_plane_h_check < 2 * m_top[i])
                    env->ThrowError("FillBorders: clip too small for top border on component %d, mode %d", i, MODE_VAL);

                if (m_bottom[i] > 0 && current_plane_h_check < 2 * m_bottom[i])
                    env->ThrowError("FillBorders: clip too small for bottom border on component %d, mode %d", i, MODE_VAL);
            }
            else if (MODE_VAL == 4)
            {
                if (current_plane_w_check < m_left[i] + m_right[i] || current_plane_h_check < m_top[i] + m_bottom[i])
                    env->ThrowError("FillBorders: borders too big for wrap mode on component %d", i);
            }
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL>
PVideoFrame __stdcall FillBorders<T_Pixel, T_Calc, MODE_VAL>::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame src_frame{child->GetFrame(n, env)};

    if (!m_interlaced) [[likely]]
    {
        if (has_at_least_v8) [[likely]]
        {
            const AVSMap* props{env->getFramePropsRO(src_frame)};

            if (env->propNumElements(props, "_FieldBased") > 0)
            {
                const int64_t field_based{env->propGetInt(props, "_FieldBased", 0, nullptr)};

                if (field_based > 0) [[unlikely]]
                    env->ThrowError("FillBorders: frame must be not interlaced or use interlaced=true.");
            }
        }
    }

    PVideoFrame dst_frame{(has_at_least_v8) ? env->NewVideoFrameP(vi, &src_frame) : env->NewVideoFrame(vi)};

    constexpr std::array<int, 4> yuv_plane_order{PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
    constexpr std::array<int, 4> rgb_plane_order{PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A};
    const int* const plane_constants{vi.IsRGB() ? rgb_plane_order.data() : yuv_plane_order.data()};

    T_Pixel temp_buf_for_gaussian[MAX_TSIZE];

    for (int i{0}; i < vi.NumComponents(); ++i)
    {
        const int current_plane{plane_constants[i]};

        if (m_process[i] == 1) [[unlikely]]
            continue;

        const int height{src_frame->GetHeight(current_plane)};
        const int width{src_frame->GetRowSize(current_plane)};
        const int src_stride{src_frame->GetPitch(current_plane)};
        const int dst_stride{dst_frame->GetPitch(current_plane)};
        const uint8_t* AVS_RESTRICT const srcp{src_frame->GetReadPtr(current_plane)};
        uint8_t* const dstp{dst_frame->GetWritePtr(current_plane)};

        env->BitBlt(dstp, dst_stride, srcp, src_stride, width, height);

        if (m_process[i] == 2) [[unlikely]]
            continue;

        const int width_processing{static_cast<int>(width / sizeof(T_Pixel))};
        const int stride_processing{static_cast<int>(dst_stride / sizeof(T_Pixel))};
        T_Pixel* AVS_RESTRICT const dstp_processing{reinterpret_cast<T_Pixel*>(dstp)};

        const int image_bits{vi.BitsPerComponent()};
        const int lerp_float_plane_category{[&]() {
            if (!vi.IsRGB())
            {
                if (current_plane == PLANAR_U)
                    return 1;
                else if (current_plane == PLANAR_V)
                    return 2;
                else
                    return 0;
            }
            else
                return 0;
        }()};

        if constexpr (MODE_VAL == 0)
            handle_mode_0_fillmargins_impl(dstp_processing, width_processing, height, stride_processing, i);
        else if constexpr (MODE_VAL == 1)
            handle_mode_1_repeat_impl(dstp_processing, width_processing, height, stride_processing, i);
        else if constexpr (MODE_VAL == 2)
            handle_mode_2_mirror_impl(dstp_processing, width_processing, height, stride_processing, i);
        else if constexpr (MODE_VAL == 3)
            handle_mode_3_reflect_impl(dstp_processing, width_processing, height, stride_processing, i);
        else if constexpr (MODE_VAL == 4)
        {
            handle_mode_4_wrap_base_impl(dstp_processing, width_processing, height, stride_processing, i);

            if (m_ts_runtime > 0) [[likely]]
                apply_mode4_transient_smoothing_impl(dstp_processing, width_processing, height, stride_processing, i, image_bits,
                    lerp_float_plane_category, temp_buf_for_gaussian);
        }
        else if constexpr (MODE_VAL == 5)
            handle_mode_5_fade_impl(dstp_processing, width_processing, height, stride_processing, i, image_bits, lerp_float_plane_category);
        else if constexpr (MODE_VAL == 6)
            handle_mode_6_fixborders_impl(dstp_processing, width_processing, height, stride_processing, i);
    }

    return dst_frame;
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_0_fillmargins_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    for (int y{m_top[component_idx]}; y < plane_height - m_bottom[component_idx]; ++y)
    {
        T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y) * stride};

        if (m_left[component_idx] > 0)
        {
            if constexpr (std::is_same_v<T_Pixel, uint8_t>)
                std::memset(current_row_ptr, current_row_ptr[m_left[component_idx]], m_left[component_idx]);
            else
                memset16<T_Pixel>(current_row_ptr, current_row_ptr[m_left[component_idx]], m_left[component_idx]);
        }

        if (m_right[component_idx] > 0)
        {
            if constexpr (std::is_same_v<T_Pixel, uint8_t>)
                std::memset(current_row_ptr + plane_width - m_right[component_idx],
                    current_row_ptr[plane_width - m_right[component_idx] - 1], m_right[component_idx]);
            else
                memset16<T_Pixel>(current_row_ptr + plane_width - m_right[component_idx],
                    current_row_ptr[plane_width - m_right[component_idx] - 1], m_right[component_idx]);
        }
    }

    for (int y{m_top[component_idx] - 1}; y >= 0; --y)
    {
        if (y + 1 >= plane_height) [[unlikely]]
            continue;

        const T_Pixel* AVS_RESTRICT const prev_row{dstp + stride * static_cast<int64_t>(y + 1)};
        T_Pixel* AVS_RESTRICT const curr_row{dstp + stride * static_cast<int64_t>(y)};

        if (plane_width > 0)
            curr_row[0] = prev_row[0];

        if (plane_width > 1)
        {
            const int num_edge_pixels_to_copy{std::min(8, plane_width - 1)};

            if (num_edge_pixels_to_copy > 0)
                std::memcpy(curr_row + plane_width - num_edge_pixels_to_copy, prev_row + plane_width - num_edge_pixels_to_copy,
                    static_cast<size_t>(num_edge_pixels_to_copy) * sizeof(T_Pixel));
        }

        for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
        {
            if (x - 1 < 0 || x + 1 >= plane_width) [[unlikely]]
                continue;

            const T_Calc p{static_cast<T_Calc>(prev_row[x - 1])};
            const T_Calc c{static_cast<T_Calc>(prev_row[x])};
            const T_Calc n{static_cast<T_Calc>(prev_row[x + 1])};

            if constexpr (std::is_integral_v<T_Pixel>)
                curr_row[x] = static_cast<T_Pixel>(std::round((3.0 * p + 2.0 * c + 3.0 * n) / 8.0));
            else
                curr_row[x] = static_cast<T_Pixel>((3 * p + 2 * c + 3 * n) / static_cast<T_Calc>(8.0));
        }
    }

    for (int y{plane_height - m_bottom[component_idx]}; y < plane_height; ++y)
    {
        if (y - 1 < 0) [[unlikely]]
            continue;

        const T_Pixel* AVS_RESTRICT const prev_row{dstp + stride * static_cast<int64_t>(y - 1)};
        T_Pixel* AVS_RESTRICT const curr_row{dstp + stride * static_cast<int64_t>(y)};

        if (plane_width > 0)
            curr_row[0] = prev_row[0];

        if (plane_width > 1)
        {
            const int num_edge_pixels_to_copy{std::min(8, plane_width - 1)};

            if (num_edge_pixels_to_copy > 0)
                std::memcpy(curr_row + plane_width - num_edge_pixels_to_copy, prev_row + plane_width - num_edge_pixels_to_copy,
                    static_cast<size_t>(num_edge_pixels_to_copy) * sizeof(T_Pixel));
        }

        for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
        {
            if (x - 1 < 0 || x + 1 >= plane_width) [[unlikely]]
                continue;

            const T_Calc p{static_cast<T_Calc>(prev_row[x - 1])};
            const T_Calc c{static_cast<T_Calc>(prev_row[x])};
            const T_Calc n{static_cast<T_Calc>(prev_row[x + 1])};

            if constexpr (std::is_integral_v<T_Pixel>)
                curr_row[x] = static_cast<T_Pixel>(std::round((3.0 * p + 2.0 * c + 3.0 * n) / 8.0));
            else
                curr_row[x] = static_cast<T_Pixel>((3 * p + 2 * c + 3 * n) / static_cast<T_Calc>(8.0));
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_1_repeat_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    for (int y{m_top[component_idx]}; y < plane_height - m_bottom[component_idx]; ++y)
    {
        T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y) * stride};

        if (m_left[component_idx] > 0)
        {
            if constexpr (std::is_same_v<T_Pixel, uint8_t>)
                std::memset(current_row_ptr, current_row_ptr[m_left[component_idx]], m_left[component_idx]);
            else
                memset16<T_Pixel>(current_row_ptr, current_row_ptr[m_left[component_idx]], m_left[component_idx]);
        }

        if (m_right[component_idx] > 0)
        {
            if constexpr (std::is_same_v<T_Pixel, uint8_t>)
                std::memset(current_row_ptr + plane_width - m_right[component_idx],
                    current_row_ptr[plane_width - m_right[component_idx] - 1], m_right[component_idx]);
            else
                memset16<T_Pixel>(current_row_ptr + plane_width - m_right[component_idx],
                    current_row_ptr[plane_width - m_right[component_idx] - 1], m_right[component_idx]);
        }
    }

    if (m_top[component_idx] > 0)
    {
        const int src_y_top{m_top[component_idx]};

        if (src_y_top < plane_height && plane_width > 0) [[likely]]
        {
            const T_Pixel* AVS_RESTRICT const src_row_ptr{dstp + static_cast<int64_t>(stride) * src_y_top};

            for (int y{0}; y < m_top[component_idx]; ++y)
                std::memcpy(dstp + static_cast<int64_t>(stride) * y, src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }
    if (m_bottom[component_idx] > 0)
    {
        const int src_y_bottom{plane_height - m_bottom[component_idx] - 1};
        if (src_y_bottom >= 0 && plane_width > 0) [[likely]]
        {
            const T_Pixel* AVS_RESTRICT const src_row_ptr{dstp + static_cast<int64_t>(stride) * src_y_bottom};

            for (int y{plane_height - m_bottom[component_idx]}; y < plane_height; ++y)
                std::memcpy(dstp + static_cast<int64_t>(stride) * y, src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_2_mirror_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    for (int y{m_top[component_idx]}; y < plane_height - m_bottom[component_idx]; ++y)
    {
        T_Pixel* AVS_RESTRICT const row_ptr{dstp + stride * static_cast<int64_t>(y)};

        for (int x{0}; x < m_left[component_idx]; ++x)
        {
            const int src_x{m_left[component_idx] * 2 - 1 - x};

            if (src_x >= 0 && src_x < plane_width) [[likely]]
                row_ptr[x] = row_ptr[src_x];
        }

        for (int x{0}; x < m_right[component_idx]; ++x)
        {
            const int src_x{plane_width - m_right[component_idx] - 1 - x};

            if (src_x >= 0 && src_x < plane_width) [[likely]]
                row_ptr[plane_width - m_right[component_idx] + x] = row_ptr[src_x];
        }
    }

    if (m_top[component_idx] > 0 && plane_width > 0)
    {
        for (int y{0}; y < m_top[component_idx]; ++y)
        {
            const int64_t src_y{m_top[component_idx] * 2LL - 1 - y};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
                std::memcpy(
                    dstp + static_cast<int64_t>(stride) * y, dstp + stride * src_y, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }

    if (m_bottom[component_idx] > 0 && plane_width > 0)
    {
        for (int y_offset{0}; y_offset < m_bottom[component_idx]; ++y_offset)
        {
            const int y_to_fill{plane_height - m_bottom[component_idx] + y_offset};
            const int64_t src_y{static_cast<int64_t>(plane_height) - m_bottom[component_idx] - 1 - y_offset};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
                std::memcpy(dstp + stride * static_cast<int64_t>(y_to_fill), dstp + stride * src_y,
                    static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_3_reflect_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    for (int y{m_top[component_idx]}; y < plane_height - m_bottom[component_idx]; ++y)
    {
        T_Pixel* AVS_RESTRICT const row_ptr{dstp + stride * static_cast<int64_t>(y)};

        for (int x{0}; x < m_left[component_idx]; ++x)
        {
            const int src_x{m_left[component_idx] * 2 - x};

            if (src_x >= 0 && src_x < plane_width) [[likely]]
                row_ptr[x] = row_ptr[src_x];
        }

        for (int x{0}; x < m_right[component_idx]; ++x)
        {
            const int src_x{plane_width - m_right[component_idx] - 2 - x};

            if (src_x >= 0 && src_x < plane_width) [[likely]]
                row_ptr[plane_width - m_right[component_idx] + x] = row_ptr[src_x];
        }
    }

    if (m_top[component_idx] > 0 && plane_width > 0)
    {
        for (int y{0}; y < m_top[component_idx]; ++y)
        {
            const int64_t src_y{m_top[component_idx] * 2LL - y};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
                std::memcpy(
                    dstp + static_cast<int64_t>(stride) * y, dstp + stride * src_y, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }

    if (m_bottom[component_idx] > 0 && plane_width > 0)
    {
        for (int y_offset{0}; y_offset < m_bottom[component_idx]; ++y_offset)
        {
            const int y_to_fill{plane_height - m_bottom[component_idx] + y_offset};
            const int64_t src_y{static_cast<int64_t>(plane_height) - m_bottom[component_idx] - 2 - y_offset};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
                std::memcpy(dstp + stride * static_cast<int64_t>(y_to_fill), dstp + stride * src_y,
                    static_cast<size_t>(plane_width) * sizeof(T_Pixel));
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_4_wrap_base_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    for (int y{m_top[component_idx]}; y < plane_height - m_bottom[component_idx]; ++y)
    {
        T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y) * stride};

        if (m_left[component_idx] > 0)
        {
            for (int x{0}; x < m_left[component_idx]; ++x)
            {
                const int src_x{plane_width - m_right[component_idx] - m_left[component_idx] + x};

                if (src_x >= 0 && src_x < plane_width) [[likely]]
                    current_row_ptr[x] = current_row_ptr[src_x];
                else if (plane_width > 0)
                    current_row_ptr[x] = current_row_ptr[0];
            }
        }
        if (m_right[component_idx] > 0)
        {
            for (int x{0}; x < m_right[component_idx]; ++x)
            {
                const int src_x{m_left[component_idx] + x};

                if (src_x >= 0 && src_x < plane_width) [[likely]]
                    current_row_ptr[plane_width - m_right[component_idx] + x] = current_row_ptr[src_x];
                else if (plane_width > 0)
                    current_row_ptr[plane_width - m_right[component_idx] + x] = current_row_ptr[plane_width - 1];
            }
        }
    }

    if (m_top[component_idx] > 0 && plane_width > 0) [[likely]]
    {
        for (int y_fill{0}; y_fill < m_top[component_idx]; ++y_fill)
        {
            const int64_t src_y{static_cast<int64_t>(plane_height) - m_bottom[component_idx] - m_top[component_idx] + y_fill};
            T_Pixel* AVS_RESTRICT const dst_row_ptr{dstp + static_cast<int64_t>(y_fill) * stride};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
            {
                const T_Pixel* AVS_RESTRICT const src_row_ptr{dstp + src_y * stride};
                std::memcpy(dst_row_ptr, src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
            }
            else if (plane_height > 0) [[unlikely]]
            {
                const T_Pixel* AVS_RESTRICT const fallback_src_row_ptr{dstp}; // Row 0
                std::memcpy(dst_row_ptr, fallback_src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
            }
        }
    }

    if (m_bottom[component_idx] > 0 && plane_width > 0) [[likely]]
    {
        for (int y_offset_in_bottom_border{0}; y_offset_in_bottom_border < m_bottom[component_idx]; ++y_offset_in_bottom_border)
        {
            const int y_fill{plane_height - m_bottom[component_idx] + y_offset_in_bottom_border};
            const int64_t src_y{static_cast<int64_t>(m_top[component_idx]) + y_offset_in_bottom_border};
            T_Pixel* AVS_RESTRICT const dst_row_ptr{dstp + static_cast<int64_t>(y_fill) * stride};

            if (src_y >= 0 && src_y < plane_height) [[likely]]
            {
                const T_Pixel* AVS_RESTRICT const src_row_ptr{dstp + src_y * stride};
                std::memcpy(dst_row_ptr, src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
            }
            else if (plane_height > 0) [[unlikely]]
            {
                const T_Pixel* AVS_RESTRICT const fallback_src_row_ptr{dstp + static_cast<int64_t>(plane_height - 1) * stride};
                std::memcpy(dst_row_ptr, fallback_src_row_ptr, static_cast<size_t>(plane_width) * sizeof(T_Pixel));
            }
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::apply_mode4_transient_smoothing_impl(T_Pixel* AVS_RESTRICT dstp, const int plane_width,
    const int plane_height, const size_t stride, const int component_idx, const int bits, const int lerp_plane_idx_param,
    T_Pixel* AVS_RESTRICT temp_buf) const noexcept
{
    const int tr_s{std::min(m_ts_runtime, MAX_TSIZE / 2)};

    if (tr_s == 0) [[unlikely]]
        return;

    if (m_left[component_idx] > 0 && tr_s <= m_left[component_idx]) [[likely]]
    {
        for (int y{0}; y < plane_height; ++y)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y) * stride};

            switch (m_ts_mode_runtime)
            {
            case 0: // Lerp Gradient
                smooth_lerp_left_impl(current_row_ptr, plane_width, m_left[component_idx], tr_s, bits, lerp_plane_idx_param);
                break;
            case 1: // Gaussian Blur - No Original Pixel Change
                smooth_gaussian_horizontal_impl(current_row_ptr, plane_width, m_left[component_idx], tr_s, true, temp_buf, false);
                break;
            case 2: // Gaussian Blur - Original Pixels Changed
                smooth_gaussian_horizontal_impl(current_row_ptr, plane_width, m_left[component_idx], tr_s, true, temp_buf, true);
                break;
            }
        }
    }

    if (m_right[component_idx] > 0 && tr_s <= m_right[component_idx]) [[likely]]
    {
        for (int y{0}; y < plane_height; ++y)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y) * stride};

            switch (m_ts_mode_runtime)
            {
            case 0:
                smooth_lerp_right_impl(current_row_ptr, plane_width, m_right[component_idx], tr_s, bits, lerp_plane_idx_param);
                break;
            case 1:
                smooth_gaussian_horizontal_impl(current_row_ptr, plane_width, m_right[component_idx], tr_s, false, temp_buf, false);
                break;
            case 2:
                smooth_gaussian_horizontal_impl(current_row_ptr, plane_width, m_right[component_idx], tr_s, false, temp_buf, true);
                break;
            }
        }
    }

    if (m_top[component_idx] > 0 && tr_s <= m_top[component_idx]) [[likely]]
    {
        for (int x{0}; x < plane_width; ++x)
        {
            T_Pixel* AVS_RESTRICT const plane_ptr_col_start{dstp + x};

            switch (m_ts_mode_runtime)
            {
            case 0:
                smooth_lerp_top_impl(plane_ptr_col_start, plane_height, stride, m_top[component_idx], tr_s, bits, lerp_plane_idx_param);
                break;
            case 1:
                smooth_gaussian_vertical_impl(plane_ptr_col_start, plane_height, stride, m_top[component_idx], tr_s, true, temp_buf, false);
                break;
            case 2:
                smooth_gaussian_vertical_impl(plane_ptr_col_start, plane_height, stride, m_top[component_idx], tr_s, true, temp_buf, true);
                break;
            }
        }
    }

    if (m_bottom[component_idx] > 0 && tr_s <= m_bottom[component_idx]) [[likely]]
    {
        for (int x{0}; x < plane_width; ++x)
        {
            T_Pixel* AVS_RESTRICT const plane_ptr_col_start{dstp + x};

            switch (m_ts_mode_runtime)
            {
            case 0:
                smooth_lerp_bottom_impl(
                    plane_ptr_col_start, plane_height, stride, m_bottom[component_idx], tr_s, bits, lerp_plane_idx_param);
                break;
            case 1:
                smooth_gaussian_vertical_impl(
                    plane_ptr_col_start, plane_height, stride, m_bottom[component_idx], tr_s, false, temp_buf, false);
                break;
            case 2:
                smooth_gaussian_vertical_impl(
                    plane_ptr_col_start, plane_height, stride, m_bottom[component_idx], tr_s, false, temp_buf, true);
                break;
            }
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_lerp_left_impl(T_Pixel* AVS_RESTRICT row_ptr, const int plane_width,
    const int border_size, const int tr_s, const int bits, const int lerp_plane_idx_param) const noexcept
{
    if (border_size == 0 || tr_s == 0) [[unlikely]]
        return;

    const int actual_tr_size{std::min(border_size, tr_s)};

    if (actual_tr_size == 0) [[unlikely]]
        return;

    const T_Calc original_edge_val{static_cast<T_Calc>(row_ptr[border_size])};
    const int anchor_x{border_size - actual_tr_size - 1};
    const T_Calc anchor_val{(anchor_x < 0) ? static_cast<T_Calc>(row_ptr[0]) : static_cast<T_Calc>(row_ptr[anchor_x])};

    for (int k{0}; k < actual_tr_size; ++k)
    {
        const int x_to_change{border_size - actual_tr_size + k};
        const int lerp_pos{k + 1};
        row_ptr[x_to_change] =
            lerp<T_Pixel, T_Calc>(original_edge_val, anchor_val, lerp_pos, actual_tr_size + 1, bits, lerp_plane_idx_param);
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_lerp_right_impl(T_Pixel* AVS_RESTRICT row_ptr, const int plane_width,
    const int border_size, const int tr_s, const int bits, const int lerp_plane_idx_param) const noexcept
{
    if (border_size == 0 || tr_s == 0) [[unlikely]]
        return;

    const int actual_tr_size{std::min(border_size, tr_s)};

    if (actual_tr_size == 0) [[unlikely]]
        return;

    const T_Calc original_edge_val{static_cast<T_Calc>(row_ptr[plane_width - border_size - 1])};
    const int anchor_x{plane_width - border_size + actual_tr_size};
    const T_Calc anchor_val{
        (anchor_x >= plane_width) ? static_cast<T_Calc>(row_ptr[plane_width - 1]) : static_cast<T_Calc>(row_ptr[anchor_x])};

    for (int k{0}; k < actual_tr_size; ++k)
    {
        const int x_to_change{plane_width - border_size + k};
        const int lerp_pos{actual_tr_size - k};
        row_ptr[x_to_change] =
            lerp<T_Pixel, T_Calc>(original_edge_val, anchor_val, lerp_pos, actual_tr_size + 1, bits, lerp_plane_idx_param);
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_lerp_top_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start, const int plane_height,
    const size_t stride, const int border_size, const int tr_s, const int bits, const int lerp_plane_idx_param) const noexcept
{
    if (border_size == 0 || tr_s == 0) [[unlikely]]
        return;

    const int actual_tr_size{std::min(border_size, tr_s)};

    if (actual_tr_size == 0) [[unlikely]]
        return;

    const T_Calc original_edge_val{static_cast<T_Calc>(plane_ptr_col_start[static_cast<int64_t>(border_size) * stride])};
    const int anchor_y{border_size - actual_tr_size - 1};
    const T_Calc anchor_val{(anchor_y < 0) ? static_cast<T_Calc>(plane_ptr_col_start[0])
                                           : static_cast<T_Calc>(plane_ptr_col_start[static_cast<int64_t>(anchor_y) * stride])};

    for (int k{0}; k < actual_tr_size; ++k)
    {
        const int y_to_change{border_size - actual_tr_size + k};
        const int lerp_pos{k + 1};
        plane_ptr_col_start[static_cast<int64_t>(y_to_change) * stride] =
            lerp<T_Pixel, T_Calc>(original_edge_val, anchor_val, lerp_pos, actual_tr_size + 1, bits, lerp_plane_idx_param);
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_lerp_bottom_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start,
    const int plane_height, const size_t stride, const int border_size, const int tr_s, const int bits,
    const int lerp_plane_idx_param) const noexcept
{
    if (border_size == 0 || tr_s == 0) [[unlikely]]
        return;

    const int actual_tr_size{std::min(border_size, tr_s)};

    if (actual_tr_size == 0) [[unlikely]]
        return;

    const T_Calc original_edge_val{static_cast<T_Calc>(plane_ptr_col_start[static_cast<int64_t>(plane_height - border_size - 1) * stride])};
    const int anchor_y{plane_height - border_size + actual_tr_size};
    const T_Calc anchor_val{(anchor_y >= plane_height)
                                ? static_cast<T_Calc>(plane_ptr_col_start[static_cast<int64_t>(plane_height - 1) * stride])
                                : static_cast<T_Calc>(plane_ptr_col_start[static_cast<int64_t>(anchor_y) * stride])};

    for (int k{0}; k < actual_tr_size; ++k)
    {
        const int y_to_change{plane_height - border_size + k};
        const int lerp_pos{actual_tr_size - k};
        plane_ptr_col_start[static_cast<int64_t>(y_to_change) * stride] =
            lerp<T_Pixel, T_Calc>(original_edge_val, anchor_val, lerp_pos, actual_tr_size + 1, bits, lerp_plane_idx_param);
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_gaussian_horizontal_impl(T_Pixel* AVS_RESTRICT row_ptr, const int plane_width,
    const int border_size, const int tr_s, const bool is_left_border, T_Pixel* AVS_RESTRICT temp_buf,
    const bool modify_original_pixels) const noexcept
{
    if (border_size == 0 || tr_s == 0 || plane_width == 0) [[unlikely]]
        return;

    const int actual_tr_size_for_op{std::min(border_size, tr_s)};

    if (actual_tr_size_for_op == 0) [[unlikely]]
        return;

    const int conv_window_full_width{actual_tr_size_for_op * 2};

    if (conv_window_full_width > MAX_TSIZE) [[unlikely]]
        return;

    const int conv_window_start_x_in_row{
        (is_left_border) ? (border_size - actual_tr_size_for_op) : (plane_width - border_size - actual_tr_size_for_op)};

    for (int xp{0}; xp < conv_window_full_width; ++xp)
    {
        const int current_center_x_in_row{conv_window_start_x_in_row + xp};
        T_Calc sum{0};

        for (int k_idx{0}; k_idx < TS_KERNELSIZE; ++k_idx)
        {
            const int sample_x{current_center_x_in_row + k_idx - (TS_KERNELSIZE / 2)};
            T_Pixel sample_val{[&]() {
                if (sample_x < 0) [[unlikely]]
                    return row_ptr[0];
                else if (sample_x >= plane_width) [[unlikely]]
                    return row_ptr[plane_width - 1];
                else [[likely]]
                    return row_ptr[sample_x];
            }()};

            sum += static_cast<T_Calc>(sample_val) * m_ts_kernel_data[k_idx];
        }

        if constexpr (std::is_integral_v<T_Pixel>)
            temp_buf[xp] = static_cast<T_Pixel>(std::round(static_cast<double>(sum)));
        else
            temp_buf[xp] = static_cast<T_Pixel>(sum);
    }

    int write_start_x_in_row_final;
    int temp_buf_read_offset_final;
    int num_pixels_to_write_final;

    if (modify_original_pixels)
    {
        write_start_x_in_row_final = conv_window_start_x_in_row;
        temp_buf_read_offset_final = 0;
        num_pixels_to_write_final = conv_window_full_width;
    }
    else
    {
        if (is_left_border)
        {
            write_start_x_in_row_final = border_size - actual_tr_size_for_op;
            temp_buf_read_offset_final = 0;
        }
        else
        {
            write_start_x_in_row_final = plane_width - border_size;
            temp_buf_read_offset_final = actual_tr_size_for_op;
        }

        num_pixels_to_write_final = actual_tr_size_for_op;
    }

    for (int k{0}; k < num_pixels_to_write_final; ++k)
    {
        const int x_to_change{write_start_x_in_row_final + k};

        if (x_to_change >= 0 && x_to_change < plane_width) [[likely]]
            row_ptr[x_to_change] = temp_buf[temp_buf_read_offset_final + k];
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::smooth_gaussian_vertical_impl(T_Pixel* AVS_RESTRICT plane_ptr_col_start,
    const int plane_height, const size_t stride, const int border_size, const int tr_s, const bool is_top_border,
    T_Pixel* AVS_RESTRICT temp_buf, const bool modify_original_pixels) const noexcept
{
    if (border_size == 0 || tr_s == 0 || plane_height == 0) [[unlikely]]
        return;

    const int actual_tr_size_for_op{std::min(border_size, tr_s)};

    if (actual_tr_size_for_op == 0) [[unlikely]]
        return;

    const int conv_window_full_width{actual_tr_size_for_op * 2};

    if (conv_window_full_width > MAX_TSIZE) [[unlikely]]
        return;

    int conv_window_start_y_in_col{
        (is_top_border) ? (border_size - actual_tr_size_for_op) : (plane_height - border_size - actual_tr_size_for_op)};

    for (int yp{0}; yp < conv_window_full_width; ++yp)
    {
        const int current_center_y_in_col{conv_window_start_y_in_col + yp};
        T_Calc sum{0};

        for (int k_idx{0}; k_idx < TS_KERNELSIZE; ++k_idx)
        {
            const int sample_y{current_center_y_in_col + k_idx - (TS_KERNELSIZE / 2)};
            T_Pixel sample_val{[&]() {
                if (sample_y < 0) [[unlikely]]
                    return plane_ptr_col_start[0 * stride];
                else if (sample_y >= plane_height) [[unlikely]]
                    return plane_ptr_col_start[static_cast<int64_t>(plane_height - 1) * stride];
                else [[likely]]
                    return plane_ptr_col_start[static_cast<int64_t>(sample_y) * stride];
            }()};

            sum += static_cast<T_Calc>(sample_val) * m_ts_kernel_data[k_idx];
        }

        if constexpr (std::is_integral_v<T_Pixel>)
            temp_buf[yp] = static_cast<T_Pixel>(std::round(static_cast<double>(sum)));
        else
            temp_buf[yp] = static_cast<T_Pixel>(sum);
    }

    int write_start_y_in_col_final;
    int temp_buf_read_offset_final;
    int num_pixels_to_write_final;

    if (modify_original_pixels)
    {
        write_start_y_in_col_final = conv_window_start_y_in_col;
        temp_buf_read_offset_final = 0;
        num_pixels_to_write_final = conv_window_full_width;
    }
    else
    {
        if (is_top_border)
        {
            write_start_y_in_col_final = border_size - actual_tr_size_for_op;
            temp_buf_read_offset_final = 0;
        }
        else
        {
            write_start_y_in_col_final = plane_height - border_size;
            temp_buf_read_offset_final = actual_tr_size_for_op;
        }

        num_pixels_to_write_final = actual_tr_size_for_op;
    }

    for (int k{0}; k < num_pixels_to_write_final; ++k)
    {
        const int y_to_change{write_start_y_in_col_final + k};

        if (y_to_change >= 0 && y_to_change < plane_height) [[likely]]
        {
            int rrr{4};
            plane_ptr_col_start[static_cast<int64_t>(y_to_change) * stride] = temp_buf[temp_buf_read_offset_final + k];
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_5_fade_impl(T_Pixel* AVS_RESTRICT dstp, const int plane_width,
    const int plane_height, const size_t stride, const int component_idx, const int bits, const int lerp_plane_idx_param) const noexcept
{
    if (plane_width == 0 || plane_height == 0) [[unlikely]]
        return;

    const int current_m_top{m_top[component_idx]};
    const int current_m_bottom{m_bottom[component_idx]};
    const int current_m_left{m_left[component_idx]};
    const int current_m_right{m_right[component_idx]};

    const bool use_constant_target = m_fade_target_value.has_value();
    const T_Calc constant_target_value_for_this_component{
        use_constant_target ? (*m_fade_target_value)[component_idx] : static_cast<T_Calc>(0)};

    if (current_m_top > 0) [[likely]]
    {
        for (int y_fill{0}; y_fill < current_m_top; ++y_fill)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y_fill) * stride};
            const T_Pixel* AVS_RESTRICT const first_row_ptr{dstp};

            for (int x_col{0}; x_col < plane_width; ++x_col)
                current_row_ptr[x_col] = lerp<T_Pixel, T_Calc>(
                    (use_constant_target) ? constant_target_value_for_this_component : static_cast<T_Calc>(first_row_ptr[x_col]),
                    static_cast<T_Calc>(current_row_ptr[x_col]), current_m_top - y_fill, current_m_top, bits, lerp_plane_idx_param);
        }
    }

    if (current_m_bottom > 0) [[likely]]
    {
        const int start_bottom_fill_y{plane_height - current_m_bottom};

        for (int y_fill{start_bottom_fill_y}; y_fill < plane_height; ++y_fill)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y_fill) * stride};
            const T_Pixel* AVS_RESTRICT const first_row_ptr{dstp};

            for (int x_col{0}; x_col < plane_width; ++x_col)
                current_row_ptr[x_col] = lerp<T_Pixel, T_Calc>(
                    (use_constant_target) ? constant_target_value_for_this_component : static_cast<T_Calc>(first_row_ptr[x_col]),
                    static_cast<T_Calc>(current_row_ptr[x_col]), y_fill - start_bottom_fill_y, current_m_bottom, bits,
                    lerp_plane_idx_param);
        }
    }

    if (current_m_left > 0) [[likely]]
    {
        for (int y_row{0}; y_row < plane_height; ++y_row)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y_row) * stride};

            for (int x_border_col{0}; x_border_col < current_m_left; ++x_border_col)
            {
                const T_Calc target_val_from_row0{static_cast<T_Calc>(dstp[x_border_col])};
                current_row_ptr[x_border_col] =
                    lerp<T_Pixel, T_Calc>((use_constant_target) ? constant_target_value_for_this_component : target_val_from_row0,
                        static_cast<T_Calc>(current_row_ptr[x_border_col]), current_m_left - x_border_col, current_m_left, bits,
                        lerp_plane_idx_param);
            }
        }
    }

    if (current_m_right > 0) [[likely]]
    {
        const int start_right_fill_x{plane_width - current_m_right};

        for (int y_row{0}; y_row < plane_height; ++y_row)
        {
            T_Pixel* AVS_RESTRICT const current_row_ptr{dstp + static_cast<int64_t>(y_row) * stride};

            for (int x_offset_in_border{0}; x_offset_in_border < current_m_right; ++x_offset_in_border)
            {
                const int x_col_to_fill{start_right_fill_x + x_offset_in_border};
                const T_Calc target_val_from_row0{static_cast<T_Calc>(dstp[x_offset_in_border])};
                current_row_ptr[x_col_to_fill] = lerp<T_Pixel, T_Calc>(
                    (use_constant_target) ? constant_target_value_for_this_component : target_val_from_row0,
                    static_cast<T_Calc>(current_row_ptr[x_col_to_fill]), x_offset_in_border, current_m_right, bits, lerp_plane_idx_param);
            }
        }
    }
}

template<typename T_Pixel, typename T_Calc, int MODE_VAL_ignored>
void FillBorders<T_Pixel, T_Calc, MODE_VAL_ignored>::handle_mode_6_fixborders_impl(
    T_Pixel* AVS_RESTRICT dstp, const int plane_width, const int plane_height, const size_t stride, const int component_idx) const noexcept
{
    if (plane_width == 0 || plane_height == 0) [[unlikely]]
        return;

    const int current_m_left{m_left[component_idx]};
    const int current_m_top{m_top[component_idx]};
    const int current_m_right{m_right[component_idx]};
    const int current_m_bottom{m_bottom[component_idx]};

    // These define how many rows/cols near the main edges are simply copied before complex averaging
    const int top_copy_zone_height{current_m_top + 3};
    const int bottom_copy_zone_height{current_m_bottom + 3};

    auto calculate_mode6_pixel{[](T_Calc prev_p, T_Calc cur_p, T_Calc next_p, T_Calc ref_prev_p, T_Calc ref_cur_p, T_Calc ref_next_p,
                                   T_Calc far_ref_prev_blur_term, T_Calc far_ref_next_blur_term) -> T_Pixel {
        T_Calc fill_prev, fill_cur, fill_next;

        if constexpr (std::is_integral_v<T_Pixel>)
        {
            fill_prev = static_cast<T_Calc>(std::llrint(
                (5.0 * static_cast<double>(prev_p) + 3.0 * static_cast<double>(cur_p) + 1.0 * static_cast<double>(next_p)) / 9.0));
            fill_cur = static_cast<T_Calc>(std::llrint(
                (1.0 * static_cast<double>(prev_p) + 3.0 * static_cast<double>(cur_p) + 1.0 * static_cast<double>(next_p)) / 5.0));
            fill_next = static_cast<T_Calc>(std::llrint(
                (1.0 * static_cast<double>(prev_p) + 3.0 * static_cast<double>(cur_p) + 5.0 * static_cast<double>(next_p)) / 9.0));
        }
        else
        {
            fill_prev = (5 * prev_p + 3 * cur_p + next_p) / static_cast<T_Calc>(9.0);
            fill_cur = (prev_p + 3 * cur_p + next_p) / static_cast<T_Calc>(5.0);
            fill_next = (prev_p + 3 * cur_p + 5 * next_p) / static_cast<T_Calc>(9.0);
        }

        const T_Calc blur_prev_val{(2 * ref_prev_p + ref_cur_p + far_ref_prev_blur_term) / static_cast<T_Calc>(4.0)};
        const T_Calc blur_next_val{(2 * ref_next_p + ref_cur_p + far_ref_next_blur_term) / static_cast<T_Calc>(4.0)};

        const T_Calc diff_next_calc{std::abs(ref_next_p - fill_cur)};
        const T_Calc diff_prev_calc{std::abs(ref_prev_p - fill_cur)};
        const T_Calc thr_next_calc{std::abs(ref_next_p - blur_next_val)};
        const T_Calc thr_prev_calc{std::abs(ref_prev_p - blur_prev_val)};

        if (diff_next_calc > thr_next_calc)
            return (diff_prev_calc < diff_next_calc) ? static_cast<T_Pixel>(fill_prev) : static_cast<T_Pixel>(fill_next);
        else if (diff_prev_calc > thr_prev_calc)
            return static_cast<T_Pixel>(fill_next);
        else
            return static_cast<T_Pixel>(fill_cur);
    }};

    // --- Left Side Processing ---
    if (current_m_left > 0) [[likely]]
    {
        for (int x_fill{current_m_left - 1}; x_fill >= 0; --x_fill)
        {
            const int x_ref1{x_fill + 1};
            const int x_ref2{x_fill + 2};

            if (x_ref1 >= plane_width) [[unlikely]]
            {
                if (x_fill + 1 < plane_width)
                {
                    for (int y{0}; y < plane_height; ++y)
                        dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_fill + 1];
                }

                continue;
            }

            // 1. Direct copy for top and bottom edge zones of this column
            for (int y{0}; y < top_copy_zone_height; ++y)
            {
                if (y < plane_height) [[likely]]
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }

            for (int y_offset_from_bottom{bottom_copy_zone_height}; y_offset_from_bottom > 0; --y_offset_from_bottom)
            {
                const int y{plane_height - y_offset_from_bottom};

                if (y >= 0 && y < plane_height) [[likely]]
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }

            // 2. Weighted average for the middle part of this column
            if (x_ref2 < plane_width) [[likely]]
            {
                for (int y{top_copy_zone_height}; y < plane_height - bottom_copy_zone_height; ++y)
                {
                    // Ensure y-2 and y+2 are valid for blur calculation's furthest lookups
                    if (y - 2 < 0 || y + 2 >= plane_height) [[unlikely]]
                    {
                        // Fallback for y too close to top/bottom for full 5-row context: simple copy
                        dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
                        continue;
                    }

                    dstp[static_cast<int64_t>(y) * stride + x_fill] =
                        calculate_mode6_pixel(static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 1) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 1) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 1) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 1) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 2) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 2) * stride + x_ref2]));
                }
            }
            else
            { // x_ref2 is out of bounds, so just copy for the middle part too
                for (int y{top_copy_zone_height}; y < plane_height - bottom_copy_zone_height; ++y)
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }
        }
    }

    // --- Right Side Processing ---
    if (current_m_right > 0) [[likely]]
    {
        for (int x_fill{plane_width - current_m_right}; x_fill < plane_width; ++x_fill)
        {
            const int x_ref1{x_fill - 1};
            const int x_ref2{x_fill - 2}; // x-2 column

            if (x_ref1 < 0) [[unlikely]]
            {
                if (x_fill - 1 >= 0)
                {
                    for (int y{0}; y < plane_height; ++y)
                        dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_fill - 1];
                }

                continue;
            }

            for (int y{0}; y < top_copy_zone_height; ++y)
            {
                if (y < plane_height) [[likely]]
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }

            for (int y_offset_from_bottom{bottom_copy_zone_height}; y_offset_from_bottom > 0; --y_offset_from_bottom)
            {
                const int y{plane_height - y_offset_from_bottom};

                if (y >= 0 && y < plane_height) [[likely]]
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }

            if (x_ref2 >= 0) [[likely]]
            {
                for (int y{top_copy_zone_height}; y < plane_height - bottom_copy_zone_height; ++y)
                {
                    if (y - 2 < 0 || y + 2 >= plane_height) [[unlikely]]
                    {
                        dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
                        continue;
                    }

                    dstp[static_cast<int64_t>(y) * stride + x_fill] =
                        calculate_mode6_pixel(static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 1) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 1) * stride + x_ref1]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 1) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 1) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y - 2) * stride + x_ref2]),
                            static_cast<T_Calc>(dstp[static_cast<int64_t>(y + 2) * stride + x_ref2]));
                }
            }
            else
            {
                for (int y{top_copy_zone_height}; y < plane_height - bottom_copy_zone_height; ++y)
                    dstp[static_cast<int64_t>(y) * stride + x_fill] = dstp[static_cast<int64_t>(y) * stride + x_ref1];
            }
        }
    }

    // --- Top Side Processing ---
    if (current_m_top > 0) [[likely]]
    {
        for (int y_fill{current_m_top - 1}; y_fill >= 0; --y_fill)
        {
            const int64_t y_fill_s{static_cast<int64_t>(y_fill)};
            const int64_t y_ref1_s{static_cast<int64_t>(y_fill + 1)}; // y+1 row
            const int64_t y_ref2_s{static_cast<int64_t>(y_fill + 2)}; // y+2 row

            if (y_ref1_s >= plane_height) [[unlikely]]
                continue;

            // 1. Direct copy for left and right edge zones of this row
            if (plane_width > 0)
                dstp[y_fill_s * stride + 0] = dstp[y_ref1_s * stride + 0];

            if (plane_width > 1)
            { // Last 8 pixels (or fewer)
                const int num_edge_pixels_to_copy{std::min(8, plane_width - 1)};

                if (num_edge_pixels_to_copy > 0)
                    std::memcpy(dstp + y_fill_s * stride + plane_width - num_edge_pixels_to_copy,
                        dstp + y_ref1_s * stride + plane_width - num_edge_pixels_to_copy,
                        static_cast<size_t>(num_edge_pixels_to_copy) * sizeof(T_Pixel));
            }

            // 2. Weighted average for the middle part of this row
            if (y_ref2_s < plane_height) [[likely]]
            { // Ensure y+2 is valid
                for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
                {
                    // Clamp x-references for edge cases instead of skipping
                    const int x_prev_clamped{std::max(0, x - 1)};
                    const int x_next_clamped{std::min(plane_width - 1, x + 1)};
                    const int x_prev2_clamped{std::max(0, x - 2)};
                    const int x_next2_clamped{std::min(plane_width - 1, x + 2)};

                    dstp[y_fill_s * stride + x] = calculate_mode6_pixel(static_cast<T_Calc>(dstp[y_ref1_s * stride + x_prev_clamped]),
                        static_cast<T_Calc>(dstp[y_ref1_s * stride + x]), static_cast<T_Calc>(dstp[y_ref1_s * stride + x_next_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_prev_clamped]), static_cast<T_Calc>(dstp[y_ref2_s * stride + x]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_next_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_prev2_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_next2_clamped]));
                }
            }
            else
            { // y_ref2 is out of bounds, just copy for the middle part too
                for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
                    dstp[y_fill_s * stride + x] = dstp[y_ref1_s * stride + x];
            }
        }
    }

    // --- Bottom Side Processing ---
    if (current_m_bottom > 0) [[likely]]
    {
        for (int y_fill{plane_height - current_m_bottom}; y_fill < plane_height; ++y_fill)
        {
            const int64_t y_fill_s{static_cast<int64_t>(y_fill)};
            const int64_t y_ref1_s{static_cast<int64_t>(y_fill - 1)}; // y-1 row
            const int64_t y_ref2_s{static_cast<int64_t>(y_fill - 2)}; // y-2 row

            if (y_ref1_s < 0) [[unlikely]]
                continue;

            if (plane_width > 0)
                dstp[y_fill_s * stride + 0] = dstp[y_ref1_s * stride + 0];

            if (plane_width > 1)
            {
                const int num_edge_pixels_to_copy{std::min(8, plane_width - 1)};

                if (num_edge_pixels_to_copy > 0)
                    std::memcpy(dstp + y_fill_s * stride + plane_width - num_edge_pixels_to_copy,
                        dstp + y_ref1_s * stride + plane_width - num_edge_pixels_to_copy,
                        static_cast<size_t>(num_edge_pixels_to_copy) * sizeof(T_Pixel));
            }

            if (y_ref2_s >= 0) [[likely]]
            {
                for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
                {
                    const int x_prev_clamped{std::max(0, x - 1)};
                    const int x_next_clamped{std::min(plane_width - 1, x + 1)};
                    const int x_prev2_clamped{std::max(0, x - 2)};
                    const int x_next2_clamped{std::min(plane_width - 1, x + 2)};

                    dstp[y_fill_s * stride + x] = calculate_mode6_pixel(static_cast<T_Calc>(dstp[y_ref1_s * stride + x_prev_clamped]),
                        static_cast<T_Calc>(dstp[y_ref1_s * stride + x]), static_cast<T_Calc>(dstp[y_ref1_s * stride + x_next_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_prev_clamped]), static_cast<T_Calc>(dstp[y_ref2_s * stride + x]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_next_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_prev2_clamped]),
                        static_cast<T_Calc>(dstp[y_ref2_s * stride + x_next2_clamped]));
                }
            }
            else
            {
                for (int x{1}; x < plane_width - std::min(8, plane_width > 0 ? plane_width - 1 : 0); ++x)
                    dstp[y_fill_s * stride + x] = dstp[y_ref1_s * stride + x];
            }
        }
    }
}

static AVSValue __cdecl Create_FillBorders(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    enum ARGS_FB
    {
        Clip,
        Left,
        Top,
        Right,
        Bottom,
        Mode,
        Y,
        U,
        V,
        A,
        Interlaced,
        Ts,
        TsMode,
        FadeValue
    };

    PClip clip{args[Clip].AsClip()};
    const VideoInfo& vi = clip->GetVideoInfo();

    const int mode{args[Mode].AsInt(0)};
    const int y_mode{args[Y].AsInt(3)};
    const int u_mode{args[U].AsInt(3)};
    const int v_mode{args[V].AsInt(3)};
    const int a_mode{args[A].AsInt(3)};
    const bool interlaced{args[Interlaced].AsBool(false)};
    const int ts{args[Ts].AsInt(0)};
    const int ts_mode{args[TsMode].AsInt(1)};

    if (mode < 0 || mode > 6)
        env->ThrowError("FillBorders: Invalid mode %d specified.", mode);

    PClip child_clip_for_constructor{interlaced ? env->Invoke("SeparateFields", clip).AsClip() : clip};

    auto instantiate_filter_helper{[&]<int CONCRETE_MODE_VAL>() -> PClip {
        if (vi.ComponentSize() == 1)
            return new FillBorders<uint8_t, int, CONCRETE_MODE_VAL>(child_clip_for_constructor, args[Left], args[Top], args[Right],
                args[Bottom], y_mode, u_mode, v_mode, a_mode, interlaced, ts, ts_mode, args[FadeValue], env);
        else if (vi.ComponentSize() == 2)
            return new FillBorders<uint16_t, int, CONCRETE_MODE_VAL>(child_clip_for_constructor, args[Left], args[Top], args[Right],
                args[Bottom], y_mode, u_mode, v_mode, a_mode, interlaced, ts, ts_mode, args[FadeValue], env);
        else
            return new FillBorders<float, float, CONCRETE_MODE_VAL>(child_clip_for_constructor, args[Left], args[Top], args[Right],
                args[Bottom], y_mode, u_mode, v_mode, a_mode, interlaced, ts, ts_mode, args[FadeValue], env);
    }};

    PClip filter{[&]() {
        switch (mode)
        {
        case 0:
            return instantiate_filter_helper.operator()<0>();
        case 1:
            return instantiate_filter_helper.operator()<1>();
        case 2:
            return instantiate_filter_helper.operator()<2>();
        case 3:
            return instantiate_filter_helper.operator()<3>();
        case 4:
            return instantiate_filter_helper.operator()<4>();
        case 5:
            return instantiate_filter_helper.operator()<5>();
        case 6:
            return instantiate_filter_helper.operator()<6>();
        default:
            return PClip{};
        }
    }()};

    if (!filter) [[unlikely]]
    {
        env->ThrowError("FillBorders: Failed to create filter instance.");
        return {};
    }

    if (interlaced)
        return env->Invoke("Weave", filter).AsClip();
    else
        return filter;
}

class Arguments
{
    AVSValue m_args[9];
    const char* m_arg_names[9];
    int _idx;

public:
    Arguments()
        : m_args{}, m_arg_names{}, _idx{}
    {
    }

    void add(AVSValue arg, const char* arg_name = nullptr)
    {
        m_args[_idx] = arg;
        m_arg_names[_idx] = arg_name;
        ++_idx;
    }

    AVSValue args() const noexcept
    {
        return {m_args, _idx};
    }

    const char* const* arg_names() const noexcept
    {
        return m_arg_names;
    }
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

    out_args->add(AVSValue(0), "mode");

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

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("FillBorders",
        "c"
        "[left]i*"
        "[top]i*"
        "[right]i*"
        "[bottom]i*"
        "[mode]i"
        "[y]i"
        "[u]i"
        "[v]i"
        "[a]i"
        "[interlaced]b"
        "[ts]i"
        "[ts_mode]i"
        "[fade_value]a",
        Create_FillBorders, 0);

    env->AddFunction("FillMargins",
        "c"
        "[left]i"
        "[top]i"
        "[right]i"
        "[bottom]i"
        "[y]i"
        "[u]i"
        "[v]i",
        Create_FillMargins, 0);
    return "FillBorders";
}
