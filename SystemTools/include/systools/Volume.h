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
public:
	using string_type = m3c::lazy_wstring<8>;

public:
	Volume(const Path& path);

public:
	const string_type& GetName() const noexcept {
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
