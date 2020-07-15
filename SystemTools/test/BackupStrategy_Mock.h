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

#pragma once

#include "systools/BackupStrategy.h"
#include "systools/DirectoryScanner.h"
#include "systools/FileComparer.h"  // IWYU pragma: keep

#include <gmock/gmock.h>

#include <vector>

#ifdef __clang_analyzer__
// Avoid collisions with Windows API defines
#undef CreateDirectory
#undef CreateHardLink
#endif

namespace systools {
class Path;
}  // namespace systools

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
