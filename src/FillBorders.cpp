#include <Windows.h>
#include <avisynth.h>
#include <inttypes.h>

enum FillMode {
   ModeFillMargins,
   ModeRepeat,
   ModeMirror
};

static void fillBorders8bit(uint8_t* dstp, int width, int height, int stride, int left, int top, int right, int bottom, int mode) {
    int x, y;

    if (mode == ModeFillMargins) {
        for (y = top; y < height - bottom; y++) {
            memset(dstp + stride * static_cast<int64_t>(y), (dstp + stride * static_cast<int64_t>(y))[left], left);
            memset(dstp + stride * static_cast<int64_t>(y) + width - right, (dstp + stride * static_cast<int64_t>(y) + width - right)[-1], right);
        }

        for (y = top - 1; y >= 0; y--) {
            // copy first pixel
            // copy last eight pixels
            dstp[stride * y] = dstp[stride * (y + 1)];
            memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8);

            // weighted average for the rest
            for (x = 1; x < width - 8; x++) {
                uint8_t prev = dstp[stride * (y + 1) + x - 1];
                uint8_t cur = dstp[stride * (y + 1) + x];
                uint8_t next = dstp[stride * (y + 1) + x + 1];
                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
            }
        }

        for (y = height - bottom; y < height; y++) {
            // copy first pixel
            // copy last eight pixels
            dstp[stride * y] = dstp[stride * (y - 1)];
            memcpy(dstp + stride * static_cast<int64_t>(y) + width - 8, dstp + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8);

            // weighted average for the rest
            for (x = 1; x < width - 8; x++) {
                uint8_t prev = dstp[stride * (y - 1) + x - 1];
                uint8_t cur = dstp[stride * (y - 1) + x];
                uint8_t next = dstp[stride * (y - 1) + x + 1];
                dstp[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
            }
        }
    }
    else if (mode == ModeRepeat) {
        for (y = top; y < height - bottom; y++) {
            memset(dstp + static_cast<int64_t>(stride) * y, (dstp + static_cast<int64_t>(stride) * y)[left], left);
            memset(dstp + static_cast<int64_t>(stride) * y + width - right, (dstp + static_cast<int64_t>(stride) * y + width - right)[-1], right);
        }

        for (y = 0; y < top; y++) {
            memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + static_cast<int64_t>(stride) * top, stride);
        }

        for (y = height - bottom; y < height; y++) {
            memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (static_cast<int64_t>(height) - bottom - 1), stride);
        }
    }
    else if (mode == ModeMirror) {
        for (y = top; y < height - bottom; y++) {
            for (x = 0; x < left; x++) {
                dstp[stride * y + x] = dstp[stride * y + left * 2 - 1 - x];
            }

            for (x = 0; x < right; x++) {
                dstp[stride * y + width - right + x] = dstp[stride * y + width - right - 1 - x];
            }
        }

        for (y = 0; y < top; y++) {
            memcpy(dstp + static_cast<int64_t>(stride) * y, dstp + stride * (top * static_cast<int64_t>(2) - 1 - y), stride);
        }

        for (y = 0; y < bottom; y++) {
            memcpy(dstp + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp + stride * (static_cast<int64_t>(height) - bottom - 1 - y), stride);
        }
    }
}

static inline void memset16(void *ptr, int value, size_t num) {
    uint16_t *tptr = (uint16_t *)ptr;
    while (num-- > 0)
        *tptr++ = (uint16_t)value;
}

static void fillBorders16bit(uint8_t* dstp, int width, int height, int stride, int left, int top, int right, int bottom, int mode) {
    int x, y;
    uint16_t* dstp16 = (uint16_t*)dstp;
    stride /= 2;

    if (mode == ModeFillMargins) {
        for (y = top; y < height - bottom; y++) {
            memset16(dstp16 + stride * static_cast<int64_t>(y), (dstp16 + stride * static_cast<int64_t>(y))[left], left);
            memset16(dstp16 + stride * static_cast<int64_t>(y) + width - right, (dstp16 + stride * static_cast<int64_t>(y) + width - right)[-1], right);
        }

        for (y = top - 1; y >= 0; y--) {
            // copy first pixel
            // copy last eight pixels
            dstp16[stride * y] = dstp16[stride * (y + 1)];
            memcpy(dstp16 + stride * static_cast<int64_t>(y) + width - 8, dstp16 + stride * (static_cast<int64_t>(y) + 1) + width - 8, 8 * 2);

            // weighted average for the rest
            for (x = 1; x < width - 8; x++) {
                uint16_t prev = dstp16[stride * (y + 1) + x - 1];
                uint16_t cur = dstp16[stride * (y + 1) + x];
                uint16_t next = dstp16[stride * (y + 1) + x + 1];
                dstp16[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
            }
        }

        for (y = height - bottom; y < height; y++) {
            // copy first pixel
            // copy last eight pixels
            dstp16[stride * y] = dstp16[stride * (y - 1)];
            memcpy(dstp16 + stride * static_cast<int64_t>(y) + width - 8, dstp16 + stride * (static_cast<int64_t>(y) - 1) + width - 8, 8 * 2);

            // weighted average for the rest
            for (x = 1; x < width - 8; x++) {
                uint16_t prev = dstp16[stride * (y - 1) + x - 1];
                uint16_t cur = dstp16[stride * (y - 1) + x];
                uint16_t next = dstp16[stride * (y - 1) + x + 1];
                dstp16[stride * y + x] = (3 * prev + 2 * cur + 3 * next + 4) / 8;
            }
        }
    }
    else if (mode == ModeRepeat) {
        for (y = top; y < height - bottom; y++) {
            memset16(dstp16 + stride * static_cast<int64_t>(y), (dstp16 + stride * static_cast<int64_t>(y))[left], left);
            memset16(dstp16 + stride * static_cast<int64_t>(y) + width - right, (dstp16 + stride * static_cast<int64_t>(y) + width - right)[-1], right);
        }

        for (y = 0; y < top; y++) {
            memcpy(dstp16 + stride * static_cast<int64_t>(y), dstp16 + static_cast<int64_t>(stride) * top, static_cast<int64_t>(stride) * 2);
        }

        for (y = height - bottom; y < height; y++) {
            memcpy(dstp16 + static_cast<int64_t>(stride) * y, dstp16 + static_cast<int64_t>(stride) * (static_cast<int64_t>(height) - bottom - static_cast<int64_t>(1)), static_cast<int64_t>(stride) * 2);
        }
    }
    else if (mode == ModeMirror) {
        for (y = top; y < height - bottom; y++) {
            for (x = 0; x < left; x++) {
                dstp16[stride * y + x] = dstp16[stride * y + left * 2 - 1 - x];
            }

            for (x = 0; x < right; x++) {
                dstp16[stride * y + width - right + x] = dstp16[stride * y + width - right - 1 - x];
            }
        }

        for (y = 0; y < top; y++) {
            memcpy(dstp16 + stride * static_cast<int64_t>(y), dstp16 + stride * (top * static_cast<int64_t>(2) - 1 - static_cast<int64_t>(y)), static_cast<int64_t>(stride) * 2);
        }

        for (y = 0; y < bottom; y++) {
            memcpy(dstp16 + stride * (static_cast<int64_t>(height) - bottom + static_cast<int64_t>(y)), dstp16 + stride * (static_cast<int64_t>(height) - bottom - 1 - static_cast<int64_t>(y)), static_cast<int64_t>(stride) * 2);
        }
    }
}

class FillBorders: public GenericVideoFilter {
	int m_left;
	int m_top;
	int m_right;
	int m_bottom;
	int m_mode;
	
public:
	FillBorders(PClip _child, int left, int top, int right, int bottom, int mode, IScriptEnvironment* env)
		: GenericVideoFilter(_child), m_left(left), m_top(top), m_right(right), m_bottom(bottom), m_mode(mode)
	{
        if (vi.BitsPerComponent() == 32) {
            env->ThrowError("FillBorders: Only constant format 8..16 bit integer input supported.");
        }
    }

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);
		
		int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
		int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
		int *planes = (vi.IsPlanarRGB() || vi.IsPlanarRGBA()) ? planes_r : planes_y;
    
		bool hasAlpha = vi.IsPlanarRGBA() || vi.IsYUVA();

		if (vi.ComponentSize() == 1) {
			for (int pid = 0; pid < (vi.IsY() ? 1 : (hasAlpha ? 4 : 3)); pid++) {
				int plane = planes[pid];
				uint8_t *dstp = frame->GetWritePtr(plane);
				int width = frame->GetRowSize(plane);
				int height = frame->GetHeight(plane);
				int stride = frame->GetPitch(plane);
				
				if (plane) {
					fillBorders8bit(dstp, width, height, stride, m_left >> vi.GetPlaneWidthSubsampling(plane), m_top >> vi.GetPlaneHeightSubsampling(plane), m_right >> vi.GetPlaneWidthSubsampling(plane), m_bottom >> vi.GetPlaneHeightSubsampling(plane), m_mode);
				} else {
					fillBorders8bit(dstp, width, height, stride, m_left, m_top, m_right, m_bottom, m_mode);
				}
			}
		} else if (vi.ComponentSize() == 2) {
			for (int pid = 0; pid < (vi.IsY() ? 1 : (hasAlpha ? 4 : 3)); pid++) {
				int plane = planes[pid];
				uint8_t *dstp = frame->GetWritePtr(plane);
				int width = frame->GetRowSize(plane) / vi.ComponentSize();
				int height = frame->GetHeight(plane);
				int stride = frame->GetPitch(plane);

				if (plane) {
					fillBorders16bit(dstp, width, height, stride, m_left >> vi.GetPlaneWidthSubsampling(plane), m_top >> vi.GetPlaneHeightSubsampling(plane), m_right >> vi.GetPlaneWidthSubsampling(plane), m_bottom >> vi.GetPlaneHeightSubsampling(plane), m_mode);
				} else {
					fillBorders16bit(dstp, width, height, stride, m_left, m_top, m_right, m_bottom, m_mode);
				}
			}
		}

      return frame;
   }
};

AVSValue __cdecl Create_FillBorders(AVSValue args, void *user_data, IScriptEnvironment *env)
{	
	if ((args[5].AsInt()) > 2) {
		env->ThrowError("FillBorders: Invalid mode. Valid values are '0 for fillmargins', '1 for repeat', and '2 for mirror'.");
	}
	
	return new FillBorders(args[0].AsClip(), args[1].AsInt(0), args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0), env);
}

AVSValue __cdecl Create_FillMargins(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	if ((args[5].AsInt()) > 0) {
		env->ThrowError("FillMargins: Invalid mode. Valid value is '0 for fillmargins'");
	}

	return new FillBorders(args[0].AsClip(), args[1].AsInt(0), args[2].AsInt(0), args[3].AsInt(0), args[4].AsInt(0), args[5].AsInt(0), env);
}

const AVS_Linkage *AVS_linkage;

extern "C" __declspec(dllexport)
const char * __stdcall AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *const vectors)
{
	AVS_linkage = vectors;

	env->AddFunction("FillBorders", "c[left]i[top]i[right]i[bottom]i[mode]i", Create_FillBorders, NULL);
	env->AddFunction("FillMargins", "c[left]i[top]i[right]i[bottom]i[mode]i", Create_FillMargins, NULL);
	return "FillBorders";
}
