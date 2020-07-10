#pragma once

#include <algorithm>
#include <optional>

namespace systools {

template <typename Src, typename Ref, typename Dst, typename Copy, typename Extra, typename Compare>
void ThreeWayMerge(Src& src, Ref& ref, Dst& dst, Copy& copy, Extra& extra, Compare compare) {
	static_assert(std::is_same_v<Src::value_type, Ref::value_type>);
	static_assert(std::is_same_v<Src::value_type, Dst::value_type>);
	static_assert(std::is_same_v<Copy::value_type, Extra::value_type>);
	static_assert(std::is_invocable_r_v<int, Compare, const Src::value_type&, const Src::value_type&>);

	const auto cmp = [compare](const Src::value_type& lhs, const Src::value_type& rhs) {
		return compare(lhs, rhs) < 0;
	};
	std::sort(src.begin(), src.end(), cmp);
	std::sort(ref.begin(), ref.end(), cmp);
	std::sort(dst.begin(), dst.end(), cmp);

	auto srcBegin = src.cbegin();
	auto refBegin = ref.cbegin();
	auto dstBegin = dst.cbegin();

	const auto srcEnd = src.cend();
	const auto refEnd = ref.cend();
	const auto dstEnd = dst.cend();

	while (true) {
		const bool hasSrc = srcBegin != srcEnd;
		const bool hasRef = refBegin != refEnd;
		const bool hasDst = dstBegin != dstEnd;
		if (!hasSrc && !hasRef && !hasDst) {
			break;
		}

		const int cmpSrcRef = (hasSrc && hasRef) ? compare(*srcBegin, *refBegin) : (hasSrc ? -1 : 1);
		const int cmpSrcDst = (hasSrc && hasDst) ? compare(*srcBegin, *dstBegin) : (hasSrc ? -1 : 1);
		const int cmpRefDst = (hasRef && hasDst) ? compare(*refBegin, *dstBegin) : (hasRef ? -1 : 1);

		if (cmpSrcRef < 0 && cmpSrcDst < 0) {
			assert(hasSrc);
			copy.emplace_back(*srcBegin, std::nullopt, std::nullopt);
			++srcBegin;
		} else if (cmpSrcRef < 0 && cmpSrcDst == 0) {
			assert(cmpRefDst > 0);
			assert(hasSrc && hasDst);
			copy.emplace_back(*srcBegin, std::nullopt, *dstBegin);
			++srcBegin;
			++dstBegin;
		} else if (cmpSrcRef == 0 && cmpSrcDst < 0) {
			assert(cmpRefDst < 0);
			assert(hasSrc && hasRef);
			copy.emplace_back(*srcBegin, *refBegin, std::nullopt);
			++srcBegin;
			++refBegin;
		} else if (cmpSrcRef == 0 && cmpSrcDst == 0) {
			assert(cmpRefDst == 0);
			assert(hasSrc && hasRef && hasDst);
			copy.emplace_back(*srcBegin, *refBegin, *dstBegin);
			++srcBegin;
			++refBegin;
			++dstBegin;
		} else if (cmpSrcRef > 0 && cmpRefDst < 0) {
			assert(hasRef);
			// exists in reference copy only -> not relevant
			++refBegin;
		} else if (cmpSrcRef > 0 && cmpRefDst == 0) {
			assert(cmpSrcDst > 0);
			assert(hasRef & hasDst);
			extra.emplace_back(std::nullopt, std::nullopt /* *refBegin not required */, *dstBegin);
			++refBegin;
			++dstBegin;
		} else if (cmpSrcDst > 0 && cmpRefDst > 0) {
			assert(hasDst);
			extra.emplace_back(std::nullopt, std::nullopt, *dstBegin);
			++dstBegin;
		} else {
			assert(false);
		}
	}
}

}  // namespace systools
