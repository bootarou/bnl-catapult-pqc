/**
*** Copyright (c) 2016-2019, Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp.
*** Copyright (c) 2020-present, Jaguar0625, gimre, BloodyRookie.
*** All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "Observers.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/observers/ObserverUtils.h"

namespace catapult { namespace observers {

	DECLARE_OBSERVER(VrfRegistrationHeight, model::VrfKeyLinkNotification)(uint32_t activationDelay) {
		return MAKE_OBSERVER(VrfRegistrationHeight, model::VrfKeyLinkNotification, ([activationDelay](
				const model::VrfKeyLinkNotification& notification,
				const ObserverContext& context) {
			auto& cache = context.Cache.sub<cache::AccountStateCache>();
			auto accountStateIter = cache.find(notification.MainAccountPublicKey);
			auto& accountState = accountStateIter.get();

			// mirror the generic vrf key link observer: when the root is (un)set, (re)set its activation height.
			// the activation delay prevents grinding on which root will win future block lotteries.
			if (ShouldLink(notification.LinkAction, context.Mode))
				accountState.SupplementalPublicKeys.setVrfRegistrationHeight(Height(context.Height.unwrap() + activationDelay));
			else
				accountState.SupplementalPublicKeys.setVrfRegistrationHeight(Height(0));
		}));
	}
}}
