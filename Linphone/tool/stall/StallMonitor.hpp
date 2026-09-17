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

#ifndef STALL_MONITOR_H_
#define STALL_MONITOR_H_

#include <QString>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// =============================================================================
// Watches the linphone thread (the "model thread") for stalls.
//
// That thread runs all SIP work from a 20ms timer calling Core::iterate(), and audio
// device setup runs on it too, inside call setup, before the SIP ACK is sent. When it
// blocks, every SIP message for that client waits with it. This class measures that.
//
// Two halves:
//   - CoreModel::onIterate() calls beat() every tick and reports its own timings. That
//     half is portable and costs a clock read and a relaxed atomic store.
//   - A plain background thread watches the heartbeat and, after a few seconds of
//     silence, captures the model thread's stack and writes it to the log.
//
// The monitor must never become the thing it is measuring. See the overhead notes in
// StallMonitor.cpp before changing any of this.
// =============================================================================

class StallMonitor {
public:
	// -- Model-thread side, read by CoreModel ---------------------------------
	// The iterate timer runs at 20ms, so anything past this is the thread being held up.
	static constexpr qint64 IterateGapWarnMs = 250;
	static constexpr qint64 IterateDurationWarnMs = 250;
	static constexpr qint64 SummaryIntervalMs = 60000;

	// -- Watchdog side --------------------------------------------------------
	// Polling at 500ms against a 3s threshold still detects within 3.0-3.5s and halves
	// the wakeups we add for the life of the process.
	static constexpr qint64 PollIntervalMs = 500;
	static constexpr qint64 StackCaptureThresholdMs = 3000;
	static constexpr qint64 StackRecaptureMs = 5000;
	static constexpr qint64 StackCaptureMinIntervalMs = 30000;
	static constexpr int MaxCapturesPerStall = 3;

	// Symbolisation is the expensive tier, so it is rate limited far harder than the
	// raw capture. The raw frames are symbolisable offline, so losing this costs little.
	static constexpr qint64 SymboliseMinIntervalMs = 300000;

	static constexpr qint64 MinidumpThresholdMs = 10000;
	static constexpr qint64 MinidumpMinIntervalMs = 600000;
	static constexpr int MaxMinidumpsPerSession = 3;

	// Symbols are pre-warmed once, but not until module loading has settled: SymInitialize
	// enumerates loaded modules under the loader lock, and doing that while the model
	// thread is still loading plugins would stall it.
	static constexpr qint64 PrewarmDelayMs = 60000;

	// Safety valves. If either is tripped the corresponding path switches itself off for
	// the rest of the session rather than degrading the app.
	static constexpr qint64 MaxAcceptableSuspendMs = 50;
	static constexpr qint64 MaxAcceptableSymboliseMs = 2000;

	static StallMonitor *getInstance();

	// Milliseconds from a monotonic clock. The single time source for this feature, so
	// that the counters in CoreModel and the watchdog agree by construction.
	static qint64 steadyMs();

	// Called on the model thread itself, from Thread::run().
	void attachToCurrentThread();
	// Stops and joins the watchdog, then releases the thread handle. Idempotent.
	void detach();

	// The watchdog only acts between these. Core startup and shutdown block the model
	// thread legitimately and must not be reported as stalls.
	void arm();
	void disarm();

	// Hot path, ~50 times a second. One relaxed atomic store, deliberately inline and
	// trivial. Do not add anything here.
	inline void beat(qint64 nowMs) {
		mHeartbeatMs.store(nowMs, std::memory_order_relaxed);
	}

private:
	StallMonitor() = default;
	~StallMonitor() = default;
	StallMonitor(const StallMonitor &) = delete;
	StallMonitor &operator=(const StallMonitor &) = delete;

	enum class Level { Message, Warning };

	void watchdogLoop();
	// The only logging entry point in this class. See the rule at the top of the .cpp.
	void logDirect(Level level, const QString &message);
	void captureStack(int captureIndex, qint64 silenceMs);
	void writeMinidump(qint64 silenceMs);

	std::atomic<qint64> mHeartbeatMs{0};
	std::atomic<bool> mArmed{false};
	std::atomic<bool> mStop{false};

	std::thread mWatchdog;
	std::mutex mWakeMutex;
	std::condition_variable mWake;

	bool mAttached = false;
	quint32 mModelThreadId = 0;
	// What attachToCurrentThread() found, held until arm() because the log file does not
	// exist yet when the model thread starts.
	QString mAttachSummary;

	// Watchdog-thread state. Only ever touched by watchdogLoop() and what it calls.
	bool mStackCaptureDisabled = false;
	bool mSymboliseDisabled = false;
	qint64 mLastCaptureMs = 0;
	qint64 mLastSymboliseMs = 0;
	qint64 mLastMinidumpMs = 0;
	int mMinidumpsThisSession = 0;
};

#endif // STALL_MONITOR_H_
