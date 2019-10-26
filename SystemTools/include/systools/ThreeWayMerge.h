#pragma once

#include <algorithm>
#include <iterator>

namespace systools {

template <typename Src, typename Ref, typename Dst, typename Copy, typename Stale>
void ThreeWayMerge(Src& src, Ref& ref, Dst& dst, Copy& copy, Stale& stale, int (*const compare)(const typename Src::value_type&, const typename Src::value_type&)) {
	static_assert(std::is_same_v<Src::value_type, Ref::value_type>);
	static_assert(std::is_same_v<Src::value_type, Dst::value_type>);
	static_assert(std::is_same_v<Copy::value_type, Stale::value_type>);

	auto srcBegin = src.begin();
	auto refBegin = ref.begin();
	auto dstBegin = dst.begin();

	const auto srcEnd = src.end();
	const auto refEnd = ref.end();
	const auto dstEnd = dst.end();

	const auto cmp = [compare](const Src::value_type& lhs, const Src::value_type& rhs) {
		return compare(lhs, rhs) < 0;
	};
	std::sort(srcBegin, srcEnd, cmp);
	std::sort(refBegin, refEnd, cmp);
	std::sort(dstBegin, dstEnd, cmp);

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
			stale.emplace_back(std::nullopt, std::nullopt, *dstBegin);  // *prvFirst not required
			++refBegin;
			++dstBegin;
		} else if (cmpSrcDst > 0 && cmpRefDst > 0) {
			assert(hasDst);
			stale.emplace_back(std::nullopt, std::nullopt, *dstBegin);
			++dstBegin;
		} else {
			assert(false);
		}
	}
}

}  // namespace systools
