// Scintilla source code edit control
/** @file PlatWin.h
 ** Implementation of platform facilities on Windows.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.
#pragma once

// sdkddkver.h
#ifndef _WIN32_WINNT_VISTA
#define _WIN32_WINNT_VISTA				0x0600
#endif
#ifndef _WIN32_WINNT_WIN7
#define _WIN32_WINNT_WIN7				0x0601
#endif
#ifndef _WIN32_WINNT_WIN8
#define _WIN32_WINNT_WIN8				0x0602
#endif
#ifndef _WIN32_WINNT_WINBLUE
#define _WIN32_WINNT_WINBLUE			0x0603
#endif
#ifndef _WIN32_WINNT_WIN10
#define _WIN32_WINNT_WIN10				0x0A00
#endif

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI		96
#endif

#if defined(_MSC_BUILD) && (_WIN32_WINNT < _WIN32_WINNT_VISTA)
#pragma warning(push)
#pragma warning(disable: 4458)
// Win32 XP v141_xp toolset with Windows 7 SDK.
// d2d1helper.h(677,19): warning C4458:  declaration of 'a' hides class member
#endif
#include <d2d1.h>
#include <dwrite.h>
#if defined(_MSC_BUILD) && (_WIN32_WINNT < _WIN32_WINNT_VISTA)
#pragma warning(pop)
#endif

// force compile C as CPP
#define NP2_FORCE_COMPILE_C_AS_CPP		1

// official Scintilla use std::call_once(), which increases binary about 12 KiB.
#define USE_STD_CALL_ONCE		0
#if !USE_STD_CALL_ONCE && (_WIN32_WINNT >= _WIN32_WINNT_VISTA)
// use InitOnceExecuteOnce()
#define USE_WIN32_INIT_ONCE		0
#else
// fallback to InterlockedCompareExchange(). it's not same as std::call_once(),
// but should safe in Windows message handlers (for WM_CREATE and SCI_SETTECHNOLOGY).
#define USE_WIN32_INIT_ONCE		0
#endif

// since Windows 10, version 1607
#if (defined(__aarch64__) || defined(_ARM64_) || defined(_M_ARM64)) && !defined(__MINGW32__)
// 1709 was the first version for Windows 10 on ARM64.
#define NP2_HAS_GETDPIFORWINDOW					1
#define GetWindowDPI(hwnd)						GetDpiForWindow(hwnd)
#define SystemMetricsForDpi(nIndex, dpi)		GetSystemMetricsForDpi((nIndex), (dpi))
#define AdjustWindowRectForDpi(lpRect, dwStyle, dwExStyle, dpi) \
		::AdjustWindowRectExForDpi((lpRect), (dwStyle), FALSE, (dwExStyle), (dpi))

#else
#define NP2_HAS_GETDPIFORWINDOW					0
#if NP2_FORCE_COMPILE_C_AS_CPP
#define NP2F_noexcept noexcept
extern UINT GetWindowDPI(HWND hwnd) noexcept;
extern int SystemMetricsForDpi(int nIndex, UINT dpi) noexcept;
extern BOOL AdjustWindowRectForDpi(LPRECT lpRect, DWORD dwStyle, DWORD dwExStyle, UINT dpi) noexcept;
#else
#define NP2F_noexcept
extern "C" UINT GetWindowDPI(HWND hwnd);
extern "C" int SystemMetricsForDpi(int nIndex, UINT dpi);
extern "C" BOOL AdjustWindowRectForDpi(LPRECT lpRect, DWORD dwStyle, DWORD dwExStyle, UINT dpi);
#endif
#endif

#if NP2_FORCE_COMPILE_C_AS_CPP
extern WCHAR defaultTextFontName[LF_FACESIZE];
#else
extern "C" WCHAR defaultTextFontName[LF_FACESIZE];
#endif

namespace Scintilla::Internal {

extern void Platform_Initialise(void *hInstance) noexcept;
extern void Platform_Finalise(bool fromDllMain) noexcept;

constexpr RECT RectFromPRectangle(PRectangle prc) noexcept {
	RECT rc = { static_cast<LONG>(prc.left), static_cast<LONG>(prc.top),
		static_cast<LONG>(prc.right), static_cast<LONG>(prc.bottom) };
	return rc;
}

constexpr PRectangle PRectangleFromRect(RECT rc) noexcept {
	return PRectangle::FromInts(rc.left, rc.top, rc.right, rc.bottom);
}

#if NP2_USE_AVX2
static_assert(sizeof(PRectangle) == sizeof(__m256d));
static_assert(sizeof(RECT) == sizeof(__m128i));

inline PRectangle PRectangleFromRectEx(RECT rc) noexcept {
	PRectangle prc;
	const __m128i i32x4 = _mm_load_si128((__m128i *)(&rc));
	const __m256d f64x4 = _mm256_cvtepi32_pd(i32x4);
	_mm256_storeu_pd((double *)(&prc), f64x4);
	return prc;
}

inline RECT RectFromPRectangleEx(PRectangle prc) noexcept {
	RECT rc;
	const __m256d f64x4 = _mm256_load_pd((double *)(&prc));
	const __m128i i32x4 = _mm256_cvttpd_epi32(f64x4);
	_mm_storeu_si128((__m128i *)(&rc), i32x4);
	return rc;
}

#else
constexpr PRectangle PRectangleFromRectEx(RECT rc) noexcept {
	return PRectangleFromRect(rc);
}

constexpr RECT RectFromPRectangleEx(PRectangle prc) noexcept {
	return RectFromPRectangle(prc);
}
#endif

constexpr POINT POINTFromPoint(Point pt) noexcept {
	return POINT { static_cast<LONG>(pt.x), static_cast<LONG>(pt.y) };
}

constexpr Point PointFromPOINT(POINT pt) noexcept {
	return Point::FromInts(pt.x, pt.y);
}

#if NP2_USE_SSE2
static_assert(sizeof(Point) == sizeof(__m128d));
static_assert(sizeof(POINT) == sizeof(__int64));

inline POINT POINTFromPointEx(Point point) noexcept {
	POINT pt;
	const __m128d f64x2 = _mm_load_pd((double *)(&point));
	const __m128i i32x2 = _mm_cvttpd_epi32(f64x2);
	_mm_storeu_si64(&pt, i32x2);
	return pt;
}

inline Point PointFromPOINTEx(POINT point) noexcept {
	Point pt;
	const __m128i i32x2 = _mm_loadu_si64(&point);
	const __m128d f64x2 = _mm_cvtepi32_pd(i32x2);
	_mm_storeu_pd((double *)(&pt), f64x2);
	return pt;
}

#else
constexpr POINT POINTFromPointEx(Point point) noexcept {
	return POINTFromPoint(point);
}

constexpr Point PointFromPOINTEx(POINT point) noexcept {
	return PointFromPOINT(point);
}
#endif

constexpr HWND HwndFromWindowID(WindowID wid) noexcept {
	return static_cast<HWND>(wid);
}

inline HWND HwndFromWindow(const Window &w) noexcept {
	return HwndFromWindowID(w.GetID());
}

inline void *PointerFromWindow(HWND hWnd) noexcept {
	return reinterpret_cast<void *>(::GetWindowLongPtr(hWnd, 0));
}

inline void SetWindowPointer(HWND hWnd, void *ptr) noexcept {
	::SetWindowLongPtr(hWnd, 0, reinterpret_cast<LONG_PTR>(ptr));
}

inline UINT DpiForWindow(WindowID wid) noexcept {
	return GetWindowDPI(HwndFromWindowID(wid));
}

HCURSOR LoadReverseArrowCursor(HCURSOR cursor, UINT dpi) noexcept;

class MouseWheelDelta {
	int wheelDelta = 0;
public:
	bool Accumulate(WPARAM wParam) noexcept {
		wheelDelta -= GET_WHEEL_DELTA_WPARAM(wParam);
		return std::abs(wheelDelta) >= WHEEL_DELTA;
	}
	int Actions() noexcept {
		const int actions = wheelDelta / WHEEL_DELTA;
		wheelDelta = wheelDelta % WHEEL_DELTA;
		return actions;
	}
};

constexpr BYTE Win32MapFontQuality(FontQuality extraFontFlag) noexcept {
	constexpr UINT mask = (DEFAULT_QUALITY << static_cast<int>(FontQuality::QualityDefault))
		| (NONANTIALIASED_QUALITY << (4 *static_cast<int>(FontQuality::QualityNonAntialiased)))
		| (ANTIALIASED_QUALITY << (4 * static_cast<int>(FontQuality::QualityAntialiased)))
		| (CLEARTYPE_QUALITY << (4 * static_cast<int>(FontQuality::QualityLcdOptimized)))
		;
	return static_cast<BYTE>((mask >> (4*static_cast<int>(extraFontFlag & FontQuality::QualityMask))) & 15);
}

extern bool LoadD2D() noexcept;
extern ID2D1Factory *pD2DFactory;
extern IDWriteFactory *pIDWriteFactory;

}
