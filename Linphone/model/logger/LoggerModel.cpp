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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QString>
#include <linphone++/linphone.hh>

#include "config.h"

#include "LoggerListener.hpp"
#include "LoggerModel.hpp"
#include "model/setting/SettingsModel.hpp"
#include "tool/Constants.hpp"
#include "tool/Utils.hpp"

#include "core/logger/QtLogger.hpp"
#include "core/path/Paths.hpp"
// -----------------------------------------------------------------------------

LoggerModel::LoggerModel(QObject *parent) : QObject(parent) {
	connect(QtLogger::getInstance(), &QtLogger::qtLogReceived, this, &LoggerModel::onQtLog, Qt::QueuedConnection);
	connect(QtLogger::getInstance(), &QtLogger::requestVerboseEnabled, this, &LoggerModel::enableVerbose,
	        Qt::QueuedConnection);
	connect(QtLogger::getInstance(), &QtLogger::requestQtOnlyEnabled, this, &LoggerModel::enableQtOnly,
	        Qt::QueuedConnection);
	connect(this, &LoggerModel::linphoneLogReceived, QtLogger::getInstance(), &QtLogger::onLinphoneLog,
	        Qt::QueuedConnection);
}

LoggerModel::~LoggerModel() {
	linphone::LoggingService::get()->removeListener(mListener);
}

bool LoggerModel::isVerbose() const {
	return mVerboseEnabled;
}

void LoggerModel::enableVerbose(bool verbose) {
	if (mVerboseEnabled != verbose) {
		mVerboseEnabled = verbose;
		emit verboseEnabledChanged();
	}
}

void LoggerModel::enableFullLogs(const bool &full) {
	auto service = linphone::LoggingService::get();
	if (service) service->setLogLevel(full ? linphone::LogLevel::Debug : linphone::LogLevel::Message);
}

bool LoggerModel::qtOnlyEnabled() const {
	return mQtOnlyEnabled;
}

void LoggerModel::enableQtOnly(const bool &enable) {
	if (mQtOnlyEnabled != enable) {
		mQtOnlyEnabled = enable;
		auto service = linphone::LoggingService::get();
		if (service) service->setDomain(enable ? Constants::AppDomain : "");
		emit qtOnlyEnabledChanged();
	}
}

// -----------------------------------------------------------------------------
// Called from Qt
void LoggerModel::onQtLog(QtMsgType type, QString msg) {
	auto service = linphone::LoggingService::get();

	auto serviceMsg = Utils::appStringToCoreString(msg);
	if (service) {
		switch (type) {
			case QtDebugMsg:
				service->debug(serviceMsg);
				break;
			case QtInfoMsg:
				service->message(serviceMsg);
				break;
			case QtWarningMsg:
				service->warning(serviceMsg);
				break;
			case QtCriticalMsg:
				service->error(serviceMsg);
				break;
			case QtFatalMsg:
				service->fatal(serviceMsg);
				break;
		}
	}
}

// Call from Linphone
void LoggerModel::onLinphoneLog(const std::shared_ptr<linphone::LoggingService> &,
                                const std::string &domain,
                                linphone::LogLevel level,
                                const std::string &message) {
	bool isAppLog = domain == Constants::AppDomain;
	if (isAppLog || !mVerboseEnabled || mQtOnlyEnabled) return; // App logs are already managed.

	emit linphoneLogReceived(domain, level, message);
}

// -----------------------------------------------------------------------------

void LoggerModel::enable(bool status) {
	linphone::Core::enableLogCollection(status ? linphone::LogCollectionState::Enabled
	                                           : linphone::LogCollectionState::Disabled);
}

// The SDK refuses to open a log file that is already over the size limit, and if its rotation cannot
// delete or rename one it stops writing to file for the rest of the session. Clear any oversize file
// up front so we can never start in that state.
//
// Returns what it did rather than logging it, because the caller in init() runs before log collection
// is enabled and we want this recorded in the log file itself, not just on the console.
QStringList LoggerModel::pruneOversizeLogs(const QString &folder) {
	QStringList report;
	QDir dir(folder);
	if (!dir.exists()) return report;
	const auto files =
	    dir.entryInfoList(QStringList(QStringLiteral(EXECUTABLE_NAME) + "*.log"), QDir::Files | QDir::NoSymLinks);
	for (const QFileInfo &fileInfo : files) {
		if (fileInfo.size() <= qint64(Constants::MaxLogsCollectionSize)) continue;
		const QString filePath = fileInfo.absoluteFilePath();
		if (QFile::remove(filePath)) {
			report << QStringLiteral("Removed oversize log file `%1` (%2 bytes).").arg(filePath).arg(fileInfo.size());
			continue;
		}
		// Something else is holding it open. We usually cannot delete it but we can still empty it,
		// which is enough for the SDK to reopen it.
		QFile file(filePath);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.close();
			report << QStringLiteral("Truncated oversize log file `%1` (%2 bytes).").arg(filePath).arg(fileInfo.size());
		} else {
			report << QStringLiteral("Could not clear oversize log file `%1`: %2.")
			              .arg(filePath)
			              .arg(file.errorString());
		}
	}
	return report;
}

void LoggerModel::applyConfig(const std::shared_ptr<linphone::Config> &config) {
	const QString folder = SettingsModel::getLogsFolder(config);
	// init() already pointed us at the default folder. Only move if the config names a different one.
	if (QDir::cleanPath(folder) !=
	    QDir::cleanPath(Utils::coreStringToAppString(linphone::Core::getLogCollectionPath()))) {
		for (const QString &line : pruneOversizeLogs(folder))
			qWarning() << line;
		linphone::Core::setLogCollectionPath(Utils::appStringToCoreString(folder));
	}
	// Re-assert the limit: resetLogCollection() puts the SDK's 10MB default back.
	linphone::Core::setLogCollectionMaxFileSize(Constants::MaxLogsCollectionSize);
	enableFullLogs(SettingsModel::getFullLogsEnabled(config));
	// TODO : uncomment when it is possible to change the config from settings
	// enable(SettingsModel::getLogsEnabled(config));
	enable(true);
}

void LoggerModel::init() {
	QLoggingCategory::setFilterRules("qt.qml.connections.warning=false");
	mListener = std::make_shared<LoggerListener>();
	connect(mListener.get(), &LoggerListener::logReceived, this, &LoggerModel::onLinphoneLog);
	{
		std::shared_ptr<linphone::LoggingService> loggingService = mLoginService = linphone::LoggingService::get();
		loggingService->setDomain(Constants::AppDomain);
		loggingService->setLogLevel(linphone::LogLevel::Debug);
		loggingService->addListener(mListener);
#ifdef _WIN32
		loggingService->enableStackTraceDumps(true);
#endif
	}
	linphone::Core::setLogCollectionPrefix(EXECUTABLE_NAME);
	linphone::Core::setLogCollectionMaxFileSize(Constants::MaxLogsCollectionSize);
	// Point the SDK at the real logs folder before enabling collection. Until this is set the SDK writes
	// to its default path, which is the working directory, and that may not be writable.
	const QString folder = Paths::getLogsDirPath();
	const QStringList pruned = pruneOversizeLogs(folder);
	linphone::Core::setLogCollectionPath(Utils::appStringToCoreString(folder));

	enable(true);

	// Only now is there a log file to write to, so report the prune after enabling.
	for (const QString &line : pruned)
		qWarning() << line;
}

QString LoggerModel::getLogText() const {
	QDir path = QString::fromStdString(linphone::Core::getLogCollectionPath());
	QString prefix = QString::fromStdString(linphone::Core::getLogCollectionPrefix());
	auto files = path.entryInfoList(QStringList(prefix + "*.log"), QDir::Files | QDir::NoSymLinks | QDir::Readable,
	                                QDir::Time | QDir::Reversed);
	QString result;
	for (auto fileInfo : files) {
		QFile file(fileInfo.filePath());
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray arr = file.readAll();
			result += QString::fromLatin1(arr);
			file.close();
		}
	}
	return result;
}
