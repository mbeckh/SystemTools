/// @file
#pragma once

#include <string>

namespace systools {

class Path;

class Volume {
public:
	Volume(const Path& path);

public:
	std::uint32_t GetUnbufferedFileOffsetAlignment();
	std::align_val_t GetUnbufferedMemoryAlignment();

private:
	void ReadUnbufferedAlignments();

private:
	std::wstring m_name;
	std::uint32_t m_unbufferedFileOffsetAlignment = 0;
	std::align_val_t m_unbufferedMemoryAlignment = static_cast<std::align_val_t>(0);
};

}  // namespace systools
