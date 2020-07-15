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

/// @file

#pragma once

#include <algorithm>
#include <cassert>
#include <optional>

namespace systools {

template <typename Src, typename Ref, typename Dst, typename Copy, typename Extra, typename Compare>
void ThreeWayMerge(Src& src, Ref& ref, Dst& dst, Copy& copy, Extra& extra, Compare compare) {
	static_assert(std::is_same_v<typename Src::value_type, typename Ref::value_type>);
	static_assert(std::is_same_v<typename Src::value_type, typename Dst::value_type>);
	static_assert(std::is_same_v<typename Copy::value_type, typename Extra::value_type>);
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
