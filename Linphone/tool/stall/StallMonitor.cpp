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

#include "StallMonitor.hpp"

#include <QDateTime>
#include <QDir>
#include <QStringList>
#include <QVector>
#include <chrono>
#include <linphone++/linphone.hh>

#include "StallPlatform.hpp"
#include "core/path/Paths.hpp"
#include "tool/Utils.hpp"

// =============================================================================
// HARD RULE FOR THE WATCHDOG THREAD
//
// watchdogLoop() and everything it calls may touch only this object's own atomics,
// StallPlatform, and linphone::LoggingService. Never CoreModel, never mCore, never App,
// never any QObject.
//
// The whole safety argument for this feature rests on that. The thread we are watching
// may be blocked anywhere, holding any lock, and the watchdog has to stay able to run
// and to write to the log while that is true.
//
// In particular, never use qWarning()/lWarning() here. LoggerModel lives on the model
// thread and its connection to QtLogger is a queued one, so a Qt log call from this
// thread would be queued onto the very thread that is stalled and could not be written
// until the stall ended. logDirect() goes straight to the SDK instead, which is
// synchronous on the calling thread and mutex protected.
//
// OVERHEAD
//
// Everything expensive here runs only against a thread that has already been silent for
// StackCaptureThresholdMs, so it is not making progress and holding it costs it nothing.
// The only continuously running cost is beat(), which is one relaxed atomic store, and
// this loop waking twice a second to read it.
//
// One bound worth knowing: a full stack is up to 62 log lines, and every line takes the
// SDK's process-wide log mutex and writes to file. That is a few milliseconds during
// which the model thread cannot log, and it only happens inside a stall we have already
// detected.
// =============================================================================

StallMonitor *StallMonitor::getInstance() {
	static StallMonitor instance;
	return &instance;
}

qint64 StallMonitor::steadyMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void StallMonitor::logDirect(Level level, const QString &message) {
	// Null until LoggerModel::init() has run, which is early but not first.
	auto service = linphone::LoggingService::get();
	if (!service) return;
	const std::string text = Utils::appStringToCoreString(QStringLiteral("[StallMonitor]: ") + message);
	switch (level) {
		case Level::Message:
			service->message(text);
			break;
		case Level::Warning:
			service->warning(text);
			break;
	}
}

void StallMonitor::attachToCurrentThread() {
	if (mAttached) return;
	mAttached = true;

	mModelThreadId = StallPlatform::registerCurrentThread();
	// Seed the heartbeat so the watchdog never sees a zero and reports the time since the
	// clock epoch as a stall.
	mHeartbeatMs.store(steadyMs(), std::memory_order_relaxed);

	// Held back rather than logged here. Thread::run() starts before LoggerModel::init(), so
	// there is no log file yet and anything written now would be lost. arm() flushes it, by
	// which point the log is up.
	if (!StallPlatform::isSupported()) {
		mAttachSummary = QStringLiteral("watching model thread, stack capture is not available on this platform so "
		                                "only stall durations will be reported");
	} else if (mModelThreadId == 0) {
		mAttachSummary = QStringLiteral("watching model thread, but could not obtain a handle to it, so only stall "
		                                "durations will be reported");
	} else {
		// Worth logging because it is what identifies the right thread in a minidump.
		mAttachSummary = QStringLiteral("watching model thread, native id %1 (0x%2)")
		                     .arg(mModelThreadId)
		                     .arg(mModelThreadId, 0, 16);
	}

	mStop.store(false, std::memory_order_relaxed);
	mWatchdog = std::thread(&StallMonitor::watchdogLoop, this);
}

void StallMonitor::detach() {
	if (!mAttached) return;
	mAttached = false;

	disarm();
	{
		std::lock_guard<std::mutex> lock(mWakeMutex);
		mStop.store(true, std::memory_order_relaxed);
	}
	mWake.notify_all();
	if (mWatchdog.joinable()) mWatchdog.join();

	// Only safe once the watchdog is joined: nothing can be using the handle now.
	StallPlatform::releaseRegisteredThread();
}

void StallMonitor::arm() {
	mHeartbeatMs.store(steadyMs(), std::memory_order_relaxed);
	mArmed.store(true, std::memory_order_relaxed);

	if (!mAttachSummary.isEmpty()) {
		logDirect(Level::Message, mAttachSummary);
		mAttachSummary.clear();
	}
}

void StallMonitor::disarm() {
	mArmed.store(false, std::memory_order_relaxed);
}

void StallMonitor::watchdogLoop() {
	qint64 armedAt = 0;
	bool prewarmed = false;

	// Per-stall state, reset whenever the thread recovers or the monitor is disarmed.
	qint64 stallStartedAt = 0;
	qint64 lastCaptureInStall = 0;
	int capturesThisStall = 0;
	bool dumpedThisStall = false;

	for (;;) {
		{
			std::unique_lock<std::mutex> lock(mWakeMutex);
			mWake.wait_for(lock, std::chrono::milliseconds(PollIntervalMs),
			               [this] { return mStop.load(std::memory_order_relaxed); });
		}
		if (mStop.load(std::memory_order_relaxed)) return;

		if (!mArmed.load(std::memory_order_relaxed)) {
			armedAt = 0;
			stallStartedAt = 0;
			capturesThisStall = 0;
			dumpedThisStall = false;
			continue;
		}

		const qint64 now = steadyMs();
		if (armedAt == 0) armedAt = now;

		// Deliberately late. SymInitialize enumerates loaded modules under the loader lock,
		// and doing that while the model thread is still loading plugins and codecs would
		// stall the thread we are here to protect.
		if (!prewarmed && !mSymboliseDisabled && StallPlatform::isSupported() && now - armedAt >= PrewarmDelayMs) {
			prewarmed = true;
			const qint64 started = steadyMs();
			const bool ok = StallPlatform::prewarmSymbols();
			const qint64 elapsed = steadyMs() - started;
			if (ok) {
				logDirect(Level::Message, QStringLiteral("symbol pre-warm completed in %1ms").arg(elapsed));
				if (elapsed > MaxAcceptableSymboliseMs) {
					mSymboliseDisabled = true;
					logDirect(Level::Warning,
					          QStringLiteral("symbolisation took %1ms, over the %2ms budget, so stacks will be "
					                         "reported as module offsets only for the rest of this session")
					              .arg(elapsed)
					              .arg(MaxAcceptableSymboliseMs));
				}
			} else {
				mSymboliseDisabled = true;
				logDirect(Level::Message, QStringLiteral("symbols are not available, so stacks will be reported as "
				                                         "module offsets only"));
			}
		}

		const qint64 silence = now - mHeartbeatMs.load(std::memory_order_relaxed);

		if (silence < StackCaptureThresholdMs) {
			if (stallStartedAt != 0) {
				// An independent measurement of the stall, taken by a thread that was never
				// stalled, so it corroborates the CoreModel figure rather than restating it.
				logDirect(Level::Warning,
				          QStringLiteral("model thread recovered after %1ms of silence").arg(now - stallStartedAt));
				stallStartedAt = 0;
				capturesThisStall = 0;
				dumpedThisStall = false;
			}
			continue;
		}

		if (stallStartedAt == 0) {
			stallStartedAt = now - silence;
			capturesThisStall = 0;
			dumpedThisStall = false;
		}

		// The first capture of a stall respects the global floor, so a flapping stall cannot
		// flood the log. Repeats within one stall respect only the per-stall spacing,
		// otherwise the global floor would make MaxCapturesPerStall unreachable.
		const bool firstOfStall = (capturesThisStall == 0);
		const bool spacingOk = firstOfStall ? (mLastCaptureMs == 0 || now - mLastCaptureMs >= StackCaptureMinIntervalMs)
		                                    : (now - lastCaptureInStall >= StackRecaptureMs);
		if (!mStackCaptureDisabled && capturesThisStall < MaxCapturesPerStall && spacingOk) {
			++capturesThisStall;
			lastCaptureInStall = now;
			mLastCaptureMs = now;
			captureStack(capturesThisStall, silence);
		}

		if (!dumpedThisStall && silence >= MinidumpThresholdMs) {
			dumpedThisStall = true;
			writeMinidump(silence);
		}
	}
}

void StallMonitor::captureStack(int captureIndex, qint64 silenceMs) {
	logDirect(Level::Warning,
	          QStringLiteral("model thread silent for %1ms, capturing stack #%2").arg(silenceMs).arg(captureIndex));

	QVector<quint64> addresses;
	QStringList frames;
	qint64 suspendMs = 0;
	if (!StallPlatform::captureRawFrames(addresses, frames, suspendMs)) {
		logDirect(Level::Warning, QStringLiteral("stack #%1 could not be captured").arg(captureIndex));
		return;
	}

	// Tier one. No DbgHelp was involved, and these are symbolisable offline against the
	// PDB, so the log is useful even when tier two never runs.
	for (int i = 0; i < frames.size(); ++i) {
		logDirect(Level::Warning, QStringLiteral("stack #%1 frame %2 %3")
		                              .arg(captureIndex)
		                              .arg(i, 2, 10, QLatin1Char('0'))
		                              .arg(frames.at(i)));
	}
	logDirect(Level::Warning, QStringLiteral("stack #%1 collected, %2 frames, suspend window %3ms")
	                              .arg(captureIndex)
	                              .arg(frames.size())
	                              .arg(suspendMs));

	// If holding the thread ever costs more than the budget, an assumption is wrong on this
	// machine and we stop rather than keep making it worse.
	if (suspendMs > MaxAcceptableSuspendMs) {
		mStackCaptureDisabled = true;
		logDirect(Level::Warning,
		          QStringLiteral("suspend window of %1ms is over the %2ms budget, so stack capture is disabled for "
		                         "the rest of this session")
		              .arg(suspendMs)
		              .arg(MaxAcceptableSuspendMs));
		return;
	}

	// Tier two, best effort and rate limited hard. Runs with the model thread already
	// resumed, so the worst it can do is delay our own log line.
	const qint64 now = steadyMs();
	if (mSymboliseDisabled || addresses.isEmpty()) return;
	if (mLastSymboliseMs != 0 && now - mLastSymboliseMs < SymboliseMinIntervalMs) return;
	mLastSymboliseMs = now;

	QStringList symbols;
	const qint64 started = steadyMs();
	const bool ok = StallPlatform::symboliseFrames(addresses, symbols);
	const qint64 elapsed = steadyMs() - started;
	if (!ok) return;

	for (int i = 0; i < symbols.size(); ++i) {
		if (symbols.at(i).isEmpty()) continue;
		logDirect(Level::Warning, QStringLiteral("stack #%1 symbols %2 %3")
		                              .arg(captureIndex)
		                              .arg(i, 2, 10, QLatin1Char('0'))
		                              .arg(symbols.at(i)));
	}
	logDirect(Level::Warning, QStringLiteral("stack #%1 symbolised in %2ms").arg(captureIndex).arg(elapsed));

	if (elapsed > MaxAcceptableSymboliseMs) {
		mSymboliseDisabled = true;
		logDirect(Level::Warning,
		          QStringLiteral("symbolisation took %1ms, over the %2ms budget, so stacks will be reported as "
		                         "module offsets only for the rest of this session")
		              .arg(elapsed)
		              .arg(MaxAcceptableSymboliseMs));
	}
}

void StallMonitor::writeMinidump(qint64 silenceMs) {
	const qint64 now = steadyMs();

	// Every skip is logged, so the log explains why a dump is missing.
	if (mMinidumpsThisSession >= MaxMinidumpsPerSession) {
		logDirect(Level::Warning, QStringLiteral("model thread silent for %1ms, minidump skipped (%2 already written "
		                                         "this session, which is the limit)")
		                              .arg(silenceMs)
		                              .arg(mMinidumpsThisSession));
		return;
	}
	if (mLastMinidumpMs != 0 && now - mLastMinidumpMs < MinidumpMinIntervalMs) {
		logDirect(Level::Warning, QStringLiteral("model thread silent for %1ms, minidump skipped (last one was %2s "
		                                         "ago, minimum interval is %3s)")
		                              .arg(silenceMs)
		                              .arg((now - mLastMinidumpMs) / 1000)
		                              .arg(MinidumpMinIntervalMs / 1000));
		return;
	}

	const QString path = Paths::getStallDumpsDirPath() +
	                     QDateTime::currentDateTime().toString(QStringLiteral("'stall-'yyyy-MM-dd-hhmmss'.dmp'"));

	logDirect(Level::Warning,
	          QStringLiteral("model thread silent for %1ms, writing a minidump to %2").arg(silenceMs).arg(path));

	const qint64 started = steadyMs();
	const bool ok = StallPlatform::writeMinidump(path);
	const qint64 elapsed = steadyMs() - started;

	if (ok) {
		mLastMinidumpMs = now;
		++mMinidumpsThisSession;
		logDirect(Level::Warning, QStringLiteral("minidump written in %1ms").arg(elapsed));
	} else {
		logDirect(Level::Warning, QStringLiteral("minidump could not be written after %1ms").arg(elapsed));
	}
}
