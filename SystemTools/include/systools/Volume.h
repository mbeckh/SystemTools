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

#include <m3c/lazy_string.h>

#include <cstdint>
#include <new>

#ifndef SYSTOOLS_NO_INLINE
#define SYSTOOLS_NO_INLINE
#endif

namespace systools {

class Path;

class Volume {
private:
	static constexpr std::uint16_t kInlineBufferSize = 8;

public:
	using string_type = m3c::lazy_wstring<kInlineBufferSize>;  // NOLINT(readability-identifier-naming): Follow naming of STL and lazy_wstring.

public:
	explicit Volume(const Path& path);

public:
	[[nodiscard]] const string_type& GetName() const noexcept {
		return m_name;
	}
	SYSTOOLS_NO_INLINE std::uint32_t GetUnbufferedFileOffsetAlignment();
	SYSTOOLS_NO_INLINE std::align_val_t GetUnbufferedMemoryAlignment();

private:
	void ReadUnbufferedAlignments();

private:
	string_type m_name;
	std::uint32_t m_unbufferedFileOffsetAlignment = 0;
	std::align_val_t m_unbufferedMemoryAlignment = static_cast<std::align_val_t>(0);
};

}  // namespace systools
