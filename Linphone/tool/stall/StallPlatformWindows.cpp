/*
 * Copyright (c) 2010-2024 Belledonne Communications SARL.
 *
 * This file is part of linphone-desktop
 * (see https://www.linphone.org).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "StallPlatform.hpp"

#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// dbghelp.h requires windows.h first.
#include <dbghelp.h>

// bctoolbox pulls dbghelp in the same way, at bctoolbox/src/utils/win_utils.cc. The CMake
// link is the real one; this is belt and braces so a first build cannot fail on it.
#pragma comment(lib, "dbghelp.lib")

// =============================================================================
// Capturing a stalled thread's stack on Windows.
//
// The danger here is not getting a bad stack, it is deadlocking. If we suspend the model
// thread while it holds the loader lock, the CRT heap lock, the DbgHelp lock or the SDK's
// log mutex, and then call anything that wants the same lock, the watchdog blocks forever
// with that thread still suspended and the app is dead. That is far worse than the stall
// we came to diagnose.
//
// So the suspend window contains the unwind and nothing else:
//   - no DbgHelp. StackWalk64 would take the DbgHelp lock internally, so we unwind with
//     RtlLookupFunctionEntry and RtlVirtualUnwind instead, which read the already-loaded
//     modules' exception directories and touch nothing of ours.
//   - no allocation. Frames go into a fixed array; the heap lock is exactly the sort of
//     thing the WASAPI path could be holding.
//   - no logging. The SDK's log write takes a process-wide mutex.
//
// Module names and symbols are both resolved afterwards, with the thread running again.
// =============================================================================

namespace {

// x64 is the only architecture we ship. The unwind below is x64-specific, so everything
// degrades to "unsupported" elsewhere rather than guessing.
#if defined(_M_X64) || defined(_M_AMD64)
#define STALL_UNWIND_SUPPORTED 1
#else
#define STALL_UNWIND_SUPPORTED 0
#endif

constexpr int MaxFrames = 62;

HANDLE gModelThread = nullptr;
DWORD gModelThreadId = 0;

// DbgHelp is single threaded and not re-entrant. This only serialises our own use of it;
// bctoolbox's crash hooks are outside it, which is discussed in symboliseFrames().
std::mutex gSymbolMutex;

QString basenameOf(const char *path) {
	const QString full = QString::fromLocal8Bit(path);
	const int slash = qMax(full.lastIndexOf(QLatin1Char('\\')), full.lastIndexOf(QLatin1Char('/')));
	return slash >= 0 ? full.mid(slash + 1) : full;
}

#if STALL_UNWIND_SUPPORTED

// POD locals only, and no C++ objects anywhere in here: MSVC rejects __try in a function
// that needs C++ unwinding (C2712). Everything this function calls is either a raw Win32
// call or an ntdll unwind primitive.
//
// Returns the number of frames collected. Resumes the thread on every path that suspended
// it.
int collectFramesSuspended(HANDLE thread, DWORD64 *out, int maxFrames) {
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;

	if (SuspendThread(thread) == (DWORD)-1) return 0;

	int count = 0;
	__try {
		// SuspendThread is asynchronous and can return before the thread has actually
		// stopped. GetThreadContext is the documented way to force that to complete, so it
		// has to come first.
		if (GetThreadContext(thread, &ctx)) {
			DWORD64 lastSp = 0;
			while (count < maxFrames && ctx.Rip != 0 && ctx.Rsp > lastSp) {
				lastSp = ctx.Rsp;
				out[count++] = ctx.Rip;

				DWORD64 imageBase = 0;
				PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
				// A leaf frame, or a frame with no unwind data. Stop cleanly rather than
				// guessing; the frame count in the log makes a short walk visible.
				if (!function) break;

				PVOID handlerData = nullptr;
				DWORD64 establisherFrame = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, function, &ctx, &handlerData,
				                 &establisherFrame, nullptr);
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// Reading the target thread's stack can fault if it is corrupt or being torn down.
		// Keep whatever we already collected.
	}

	ResumeThread(thread);
	return count;
}

// Same unwind, but against our own live context. Used only to pre-warm symbols.
int collectOwnFrames(DWORD64 *out, int maxFrames) {
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;
	RtlCaptureContext(&ctx);

	int count = 0;
	__try {
		DWORD64 lastSp = 0;
		while (count < maxFrames && ctx.Rip != 0 && ctx.Rsp > lastSp) {
			lastSp = ctx.Rsp;
			out[count++] = ctx.Rip;

			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
			if (!function) break;

			PVOID handlerData = nullptr;
			DWORD64 establisherFrame = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, function, &ctx, &handlerData, &establisherFrame,
			                 nullptr);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return count;
}

#endif // STALL_UNWIND_SUPPORTED

// Turns a return address into "module.dll+0x1234", without DbgHelp. Safe to call only
// once the target thread is running again, because it takes the loader lock briefly.
QString formatRawFrame(DWORD64 address) {
	HMODULE module = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCSTR>(address), &module) &&
	    module) {
		char path[MAX_PATH] = {0};
		if (GetModuleFileNameA(module, path, MAX_PATH)) {
			const DWORD64 offset = address - reinterpret_cast<DWORD64>(module);
			return QStringLiteral("%1+0x%2").arg(basenameOf(path)).arg(offset, 0, 16);
		}
	}
	return QStringLiteral("0x%1").arg(address, 16, 16, QLatin1Char('0'));
}

// Resolves function name and source line for as many addresses as it can. Assumes the
// caller already holds gSymbolMutex and has initialised the symbol handler.
void resolveSymbols(HANDLE process, const QVector<quint64> &addresses, QStringList &symbols) {
	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)] = {0};
	PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYM_NAME;

	for (const quint64 address : addresses) {
		QString line;

		DWORD64 displacement = 0;
		if (SymFromAddr(process, address, &displacement, symbol)) {
			QString module;
			HMODULE handle = nullptr;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                       reinterpret_cast<LPCSTR>(address), &handle) &&
			    handle) {
				char path[MAX_PATH] = {0};
				if (GetModuleFileNameA(handle, path, MAX_PATH)) module = basenameOf(path) + QLatin1Char('!');
			}
			line = module + QString::fromLocal8Bit(symbol->Name);

			IMAGEHLP_LINE64 sourceLine;
			memset(&sourceLine, 0, sizeof(sourceLine));
			sourceLine.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			DWORD lineDisplacement = 0;
			if (SymGetLineFromAddr64(process, address, &lineDisplacement, &sourceLine) && sourceLine.FileName) {
				line += QStringLiteral(" (%1:%2)")
				            .arg(basenameOf(sourceLine.FileName))
				            .arg(static_cast<int>(sourceLine.LineNumber));
			}
		}
		symbols.append(line);
	}
}

} // namespace

namespace StallPlatform {

bool isSupported() {
	return STALL_UNWIND_SUPPORTED != 0;
}

quint32 registerCurrentThread() {
	// GetCurrentThread() is a pseudo-handle that only means anything to the calling thread,
	// so it has to be duplicated. Duplicating also cannot lose a race against thread id
	// reuse, which OpenThread(GetCurrentThreadId()) could.
	HANDLE duplicate = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &duplicate,
	                     THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, 0)) {
		return 0;
	}

	gModelThread = duplicate;
	gModelThreadId = GetCurrentThreadId();
	return static_cast<quint32>(gModelThreadId);
}

void releaseRegisteredThread() {
	if (gModelThread) {
		CloseHandle(gModelThread);
		gModelThread = nullptr;
	}
	gModelThreadId = 0;
}

bool captureRawFrames(QVector<quint64> &addresses, QStringList &frames, qint64 &suspendMs) {
	suspendMs = 0;
#if !STALL_UNWIND_SUPPORTED
	Q_UNUSED(addresses)
	Q_UNUSED(frames)
	return false;
#else
	if (!gModelThread) return false;
	// Suspending ourselves would hang the watchdog for good.
	if (GetCurrentThreadId() == gModelThreadId) return false;

	DWORD64 raw[MaxFrames] = {0};

	LARGE_INTEGER frequency;
	LARGE_INTEGER started;
	LARGE_INTEGER finished;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&started);

	const int count = collectFramesSuspended(gModelThread, raw, MaxFrames);

	QueryPerformanceCounter(&finished);
	if (frequency.QuadPart > 0)
		suspendMs = ((finished.QuadPart - started.QuadPart) * 1000) / frequency.QuadPart;

	if (count <= 0) return false;

	// Only now that the thread is running again do we touch the loader lock.
	addresses.reserve(count);
	for (int i = 0; i < count; ++i) {
		addresses.append(static_cast<quint64>(raw[i]));
		frames.append(formatRawFrame(raw[i]));
	}
	return true;
#endif
}

bool symboliseFrames(const QVector<quint64> &addresses, QStringList &symbols) {
	if (addresses.isEmpty()) return false;

	std::lock_guard<std::mutex> lock(gSymbolMutex);
	HANDLE process = GetCurrentProcess();

	// Initialise and clean up per capture, exactly as bctoolbox's crash hook does at
	// bctoolbox/src/utils/win_utils.cc. Holding the symbol handler open would be cheaper,
	// but then bctoolbox's own SymInitialize would fail and every crash stack in the
	// product would degrade to "Unknown Name". Speeding up a diagnostic is not worth that.
	//
	// The residual race, a crash hook running mid-capture and calling SymCleanup under us,
	// cannot be closed without patching bctoolbox. It is a few milliseconds a handful of
	// times a session, against a path we are already dying on.
	if (!SymInitialize(process, nullptr, TRUE)) return false;
	// DEFERRED_LOADS keeps the invade above from reading every PDB up front.
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);

	resolveSymbols(process, addresses, symbols);

	SymCleanup(process);
	return true;
}

bool prewarmSymbols() {
#if !STALL_UNWIND_SUPPORTED
	return false;
#else
	DWORD64 raw[MaxFrames] = {0};
	const int count = collectOwnFrames(raw, MaxFrames);
	if (count <= 0) return false;

	QVector<quint64> addresses;
	addresses.reserve(count);
	for (int i = 0; i < count; ++i)
		addresses.append(static_cast<quint64>(raw[i]));

	// Result is discarded. The point is to page in dbghelp.dll and force first touch of the
	// PDB now, rather than when the app is already in trouble.
	QStringList discarded;
	return symboliseFrames(addresses, discarded);
#endif
}

bool writeMinidump(const QString &filePath) {
	HANDLE file = CreateFileW(reinterpret_cast<const wchar_t *>(filePath.utf16()), GENERIC_WRITE, 0, nullptr,
	                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;

	// Normal plus thread info gives every thread's stack, which is what we are after, and
	// keeps the file to a few megabytes rather than tens.
	const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo);

	std::lock_guard<std::mutex> lock(gSymbolMutex);
	const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, nullptr, nullptr,
	                                  nullptr);

	CloseHandle(file);
	return ok != FALSE;
}

} // namespace StallPlatform
