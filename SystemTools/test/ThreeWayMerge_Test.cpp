/*
Copyright 2020 Michael Beckh

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "systools/ThreeWayMerge.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace systools::test {

namespace t = testing;

struct Match {
	Match() noexcept = default;
	Match(std::optional<int> src, std::optional<int> ref, std::optional<int> dst) noexcept
		: src(std::move(src))
		, ref(std::move(ref))
		, dst(std::move(dst)) {
	}
	Match(const Match&) noexcept = default;
	Match(Match&&) noexcept = default;
	~Match() noexcept = default;

	Match& operator=(const Match&) noexcept = default;
	Match& operator=(Match&&) noexcept = default;

	std::optional<int> src;
	std::optional<int> ref;
	std::optional<int> dst;
};

bool operator==(const Match& lhs, const Match& rhs) noexcept {
	return lhs.src == rhs.src && lhs.ref == rhs.ref && lhs.dst == rhs.dst;
}

void PrintTo(const Match& match, std::ostream* const os) {
	if (match.src.has_value()) {
		*os << "(" << *match.src << "/";
	} else {
		*os << "(-/";
	}
	if (match.ref.has_value()) {
		*os << *match.ref << "/";
	} else {
		*os << "-/";
	}
	if (match.dst.has_value()) {
		*os << *match.dst << "/)";
	} else {
		*os << "-/)";
	}
}

int Compare(const int& lhs, const int& rhs) noexcept {
	return lhs - rhs;
}

TEST(ThreeWayMerge_Test, call_Values_ReturnResult) {
	std::vector<int> src{3, 1, 4, 0};
	std::vector<int> ref{5, 4, 7, 8, 1};
	std::vector<int> dst{4, 2, 5, 3, 8, 9};

	std::vector<Match> copy;
	std::vector<Match> extra;

	ThreeWayMerge(src, ref, dst, copy, extra, Compare);

	EXPECT_THAT(copy, t::ElementsAre(Match{0, std::nullopt, std::nullopt}, Match{1, 1, std::nullopt}, Match{3, std::nullopt, 3}, Match{4, 4, 4}));
	EXPECT_THAT(extra, t::ElementsAre(Match{std::nullopt, std::nullopt, 2}, Match{std::nullopt, std::nullopt, 5}, Match{std::nullopt, std::nullopt, 8}, Match{std::nullopt, std::nullopt, 9}));
}

TEST(ThreeWayMerge_Test, call_CompareThrows_ThrowException) {
	std::vector<int> src{3, 1, 4, 0};
	std::vector<int> ref{5, 4, 7, 8, 1};
	std::vector<int> dst{4, 2, 5, 3, 8, 9};

	std::vector<Match> copy;
	std::vector<Match> extra;

	EXPECT_THROW(ThreeWayMerge(src, ref, dst, copy, extra, [](const int&, const int&) -> int {
					 throw std::exception();
				 }),
				 std::exception);
}

TEST(ThreeWayMerge_Test, call_Empty_ReturnEmpty) {
	std::vector<int> src;
	std::vector<int> ref;
	std::vector<int> dst;

	std::vector<Match> copy;
	std::vector<Match> extra;

	ThreeWayMerge(src, ref, dst, copy, extra, Compare);

	EXPECT_THAT(copy, t::IsEmpty());
	EXPECT_THAT(extra, t::IsEmpty());
}

}  // namespace systools::test
