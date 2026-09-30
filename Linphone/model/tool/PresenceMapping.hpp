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

#ifndef PRESENCE_MAPPING_H_
#define PRESENCE_MAPPING_H_

#include "tool/LinphoneEnums.hpp"

#include <algorithm>
#include <vector>

// =============================================================================
// The decision behind ToolModel::corePresenceModelToAppPresence(), kept free of SDK objects so it can
// be exercised without a core. It takes the basic status and every activity type in document order.
//
// The server's call-state bridge adds an <on-the-phone/> activity while an extension is on a call.
// Flexisip merges it with the user's own publication, so it can land at any index (and briefly appear
// twice). Any OnThePhone therefore means OnCall, and it outranks the user's manual status. Without it,
// the first activity is the user's own and maps exactly as it always has.
// See nm-pbx-docs/call-state-presence.md.
// =============================================================================

namespace PresenceMapping {

inline LinphoneEnums::Presence toAppPresence(linphone::PresenceBasicStatus basicStatus,
                                             const std::vector<linphone::PresenceActivity::Type> &activityTypes) {
	if (basicStatus != linphone::PresenceBasicStatus::Open) return LinphoneEnums::Presence::Undefined;

	if (std::find(activityTypes.begin(), activityTypes.end(), linphone::PresenceActivity::Type::OnThePhone) !=
	    activityTypes.end())
		return LinphoneEnums::Presence::OnCall;

	if (activityTypes.empty()) return LinphoneEnums::Presence::Online;
	switch (activityTypes.front()) {
		case linphone::PresenceActivity::Type::Busy:
			return LinphoneEnums::Presence::Busy;
		case linphone::PresenceActivity::Type::Away:
			return LinphoneEnums::Presence::Away;
		case linphone::PresenceActivity::Type::PermanentAbsence:
			return LinphoneEnums::Presence::Offline;
		case linphone::PresenceActivity::Type::Other:
			return LinphoneEnums::Presence::DoNotDisturb;
		default:
			return LinphoneEnums::Presence::Undefined;
	}
}

} // namespace PresenceMapping

#endif
