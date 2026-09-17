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

// =============================================================================
// Stub for every platform but Windows. The stall investigation this was written for is
// a Windows problem and we only ship the 64-bit Windows build, so nothing here is
// implemented. The watchdog still reports how long the model thread was silent; it just
// cannot say what it was doing.
// =============================================================================

namespace StallPlatform {

bool isSupported() {
	return false;
}

quint32 registerCurrentThread() {
	return 0;
}

void releaseRegisteredThread() {
}

bool captureRawFrames(QVector<quint64> &addresses, QStringList &frames, qint64 &suspendMs) {
	Q_UNUSED(addresses)
	Q_UNUSED(frames)
	suspendMs = 0;
	return false;
}

bool symboliseFrames(const QVector<quint64> &addresses, QStringList &symbols) {
	Q_UNUSED(addresses)
	Q_UNUSED(symbols)
	return false;
}

bool prewarmSymbols() {
	return false;
}

bool writeMinidump(const QString &filePath) {
	Q_UNUSED(filePath)
	return false;
}

} // namespace StallPlatform
