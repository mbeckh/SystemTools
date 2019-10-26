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

int Compare(const int& lhs, const int& rhs) noexcept {
	return lhs - rhs;
}

TEST(ThreeWayMerge, call_Values_ReturnResult) {
	std::vector<int> src{3, 1, 4};
	std::vector<int> ref{5, 4, 1};
	std::vector<int> dst{4, 2, 5, 3};

	std::vector<Match> copy;
	std::vector<Match> stale;

	ThreeWayMerge(src, ref, dst, copy, stale, Compare);

	EXPECT_THAT(copy, t::ElementsAre(Match{1, 1, std::nullopt}, Match{3, std::nullopt, 3}, Match{4, 4, 4}));
	EXPECT_THAT(stale, t::ElementsAre(Match{std::nullopt, std::nullopt, 2}, Match{std::nullopt, std::nullopt, 5}));
}

TEST(ThreeWayMerge, call_CompareThrows_ThrowException) {
	std::vector<int> src{3, 1, 4};
	std::vector<int> ref{5, 4, 1};
	std::vector<int> dst{4, 2, 5, 3};

	std::vector<Match> copy;
	std::vector<Match> stale;

	EXPECT_THROW(ThreeWayMerge(src, ref, dst, copy, stale, [](const int&, const int&) -> int {
					 throw std::exception();
				 }),
				 std::exception);
}

TEST(ThreeWayMerge, call_Empty_ReturnEmpty) {
	std::vector<int> src;
	std::vector<int> ref;
	std::vector<int> dst;

	std::vector<Match> copy;
	std::vector<Match> stale;

	ThreeWayMerge(src, ref, dst, copy, stale, Compare);

	EXPECT_THAT(copy, t::IsEmpty());
	EXPECT_THAT(stale, t::IsEmpty());
}

}  // namespace systools::test
