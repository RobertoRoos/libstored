/*
 * libstored, distributed debuggable data stores.
 * Copyright (C) 2020-2022  Jochem Rutgers
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "libstored/poller.h"
#include "gtest/gtest.h"

namespace {

TEST(Weak, Default)
{
	// poll_once() has not been specified, its default (weak) implementation is used.

	bool flag = false;

	auto p = stored::pollable(
		[&](stored::Pollable const& pollable) {
			flag = true;
			return pollable.events;
		},
		stored::Pollable::PollIn);

	stored::CustomPoller<stored::LoopPoller> poller = {p};
	auto res = poller.poll();
	EXPECT_EQ(res.size(), 1U);
	EXPECT_TRUE(flag);
}

} // namespace
