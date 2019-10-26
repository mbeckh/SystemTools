#pragma once

#include <systools/DirectoryScanner.h>

#include <optional>
#include <vector>

namespace systools {

class Path;

class Backup {
private:
	struct Match;

public:
	void CompareDirectories(const std::vector<Path>& src, const Path& ref, const Path& dst);

private:
	void CompareDirectories(const std::optional<Path>& optionalSrc, const std::optional<Path>& optionalRef, const Path& dst, const std::vector<Match>& directories);

private:
	DirectoryScanner m_srcScanner;
	DirectoryScanner m_refScanner;
	DirectoryScanner m_dstScanner;
};

}  // namespace systools
