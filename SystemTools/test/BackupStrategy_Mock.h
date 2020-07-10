#pragma once

#include "TestUtils.h"
#include "systools/BackupStrategy.h"
#include "systools/DirectoryScanner.h"
#include "systools/FileComparer.h"
#include "systools/Path.h"

#include <vector>

namespace systools::test {

class BackupStrategy_Mock : public BackupStrategy {
public:
	BackupStrategy_Mock();
	virtual ~BackupStrategy_Mock();

public:
	// Path Operations
	MOCK_METHOD(bool, Exists, (const Path& path), (const, override));
	MOCK_METHOD(bool, IsDirectory, (const Path& path), (const, override));

	// File Operations
	MOCK_METHOD(bool, Compare, (const Path& src, const Path& target, FileComparer& fileComparer), (const, override));
	MOCK_METHOD(void, CreateDirectory, (const Path& path, const Path& templatePath, const ScannedFile& securitySource), (const, override));
	MOCK_METHOD(void, CreateDirectoryRecursive, (const Path& path), (const, override));
	MOCK_METHOD(void, SetAttributes, (const Path& target, const ScannedFile& attributesSource), (const, override));
	MOCK_METHOD(void, SetSecurity, (const Path& path, const ScannedFile& securitySource), (const, override));
	MOCK_METHOD(void, Rename, (const Path& existingName, const Path& newName), (const, override));
	MOCK_METHOD(void, Copy, (const Path& source, const Path& target), (const, override));
	MOCK_METHOD(void, CreateHardLink, (const Path& path, const Path& existing), (const, override));
	MOCK_METHOD(void, Delete, (const Path& path), (const, override));

	// Scan Operations
	MOCK_METHOD(void, Scan, (const Path& path, DirectoryScanner& scanner, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, DirectoryScanner::Flags flags, const ScannerFilter& filter), (const, override));
	MOCK_METHOD(void, WaitForScan, (DirectoryScanner & scanner), (const, override));
};

}  // namespace systools::test
