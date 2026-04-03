/**
 * @file msvc_compat.cpp
 * @brief Compatibility shim for linking MSVC-compiled libraries with MinGW.
 *
 * When building under MinGW and linking against static libraries that were
 * compiled with MSVC (e.g. LibRaw), a number of CRT symbols are expected
 * that MinGW does not export by default. This translation unit provides
 * stub implementations for those symbols so that the linker can resolve
 * them without pulling in the MSVC runtime.
 *
 * IMPORTANT: <stdio.h> is intentionally NOT included here. Including it
 * would conflict with MinGW's own inline definitions of the same functions.
 * Instead, the required MinGW CRT functions are forward-declared and called
 * through thin wrappers.
 *
 * Copyright (C) 2024-2026 TStar Team
 */

#ifdef __MINGW32__

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

extern "C" {

// ============================================================================
// Forward declarations of types and MinGW CRT functions
// ============================================================================

struct _iobuf;
typedef struct _iobuf FILE;

extern int vsnprintf(char* s, size_t n, const char* format, va_list arg);
extern int vfprintf(FILE* stream, const char* format, va_list arg);
extern int vsscanf(const char* s, const char* format, va_list arg);

// ============================================================================
// Section 1: MSVC buffer security check stubs (/GS flag)
// ============================================================================

void __GSHandlerCheck()    {}
void __GSHandlerCheck_EH4() {}

uintptr_t __security_cookie = 0xBBADF00D;

void __security_check_cookie(uintptr_t cookie)
{
    (void)cookie;
}

// ============================================================================
// Section 2: Standard C I/O function stubs
//
// These provide the external symbols that MSVC-built object files reference.
// Each one delegates to the corresponding MinGW variadic counterpart.
// ============================================================================

int sprintf(char* str, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    // Use a generous upper bound; the caller is responsible for buffer size.
    int result = vsnprintf(str, 1048576, format, args);
    va_end(args);
    return result;
}

int sscanf(const char* str, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsscanf(str, format, args);
    va_end(args);
    return result;
}

int fprintf(FILE* stream, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

// ============================================================================
// Section 3: UCRT internal common functions (MSVC 2015+ / Universal CRT)
//
// Libraries compiled against the Universal CRT call these instead of the
// traditional C functions. We redirect them to the MinGW equivalents.
// ============================================================================

int __stdio_common_vsprintf(unsigned __int64 options, char* str,
                            size_t len, const char* format,
                            void* locale, va_list valist)
{
    (void)options;
    (void)locale;
    return vsnprintf(str, len, format, valist);
}

int __stdio_common_vsscanf(unsigned __int64 options, const char* input,
                           size_t length, const char* format,
                           void* locale, va_list valist)
{
    (void)options;
    (void)length;
    (void)locale;
    return vsscanf(input, format, valist);
}

int __stdio_common_vfprintf(unsigned __int64 options, FILE* stream,
                            const char* format, void* locale,
                            va_list valist)
{
    (void)options;
    (void)locale;
    return vfprintf(stream, format, valist);
}

// ============================================================================
// Section 4: Miscellaneous MSVC CRT stubs
// ============================================================================

/**
 * @brief No-op replacement for MSVC's invalid-parameter handler.
 *
 * Called by the CRT when a security-critical parameter check fails.
 * Under MinGW we simply suppress it.
 */
void _invoke_watson(const wchar_t* expression, const wchar_t* function,
                    const wchar_t* file, unsigned int line,
                    uintptr_t reserved)
{
    (void)expression;
    (void)function;
    (void)file;
    (void)line;
    (void)reserved;
}

} // extern "C"

#endif // __MINGW32__