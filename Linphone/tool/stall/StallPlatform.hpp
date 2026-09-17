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

#ifndef STALL_PLATFORM_H_
#define STALL_PLATFORM_H_

#include <QString>
#include <QStringList>
#include <QVector>

// =============================================================================
// The native half of the stall watchdog. Implemented for Windows in
// StallPlatformWindows.cpp and stubbed out everywhere else in StallPlatformGeneric.cpp.
//
// No platform types appear in these signatures on purpose: it means StallMonitor.cpp
// needs no #ifdef at all, and no translation unit but the Windows one ever sees
// windows.h.
// =============================================================================

namespace StallPlatform {

// False on platforms where we cannot capture another thread's stack. The watchdog still
// runs and still reports stall durations; it just has no stack to show.
bool isSupported();

// Called on the model thread itself. Returns its native thread id, or 0 on failure.
quint32 registerCurrentThread();
// Must only be called once the watchdog has been joined.
void releaseRegisteredThread();

// Tier one. Suspends the registered thread, unwinds it, and resumes it.
//
// Nothing that could take a lock the suspended thread might hold happens inside the
// suspend window: no DbgHelp, no allocation, no logging. Only the unwind itself. The
// module names are resolved afterwards, once the thread is running again.
//
// `addresses` receives the raw return addresses for tier two, `frames` the
// "module.dll+0x1234" form that is symbolisable offline, and `suspendMs` how long the
// thread was actually held.
bool captureRawFrames(QVector<quint64> &addresses, QStringList &frames, qint64 &suspendMs);

// Tier two. Best effort, uses DbgHelp, and must only ever run with the model thread
// resumed. One entry per resolvable address, already formatted.
bool symboliseFrames(const QVector<quint64> &addresses, QStringList &symbols);

// Resolves and discards our own stack, to page in DbgHelp and the PDB before a real
// capture needs them. Returns false if symbolisation is unavailable.
bool prewarmSymbols();

// Writes a minidump of the whole process. Takes the DbgHelp lock and writes megabytes,
// so it must never run with the model thread suspended.
bool writeMinidump(const QString &filePath);

} // namespace StallPlatform

#endif // STALL_PLATFORM_H_
