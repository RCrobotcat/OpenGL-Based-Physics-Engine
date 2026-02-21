#include "aligned_alloc.h"

#include <malloc.h>
#include <stdlib.h>

namespace P3D
{
    void *aligned_malloc(size_t size, size_t align)
    {
        // 修改判断：如果是 MSVC 或者 Windows 平台 (包括 MinGW)，都使用 _aligned_malloc
#if defined(_MSC_VER) || defined(_WIN32) || defined(__MINGW32__)
        return _aligned_malloc(size, align);
#else
        // Linux / Mac 等 POSIX 系统使用标准 aligned_alloc
        return aligned_alloc(align, size);
#endif
    }

    void aligned_free(void *ptr)
    {
#if defined(_MSC_VER) || defined(_WIN32) || defined(__MINGW32__)
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }
};
