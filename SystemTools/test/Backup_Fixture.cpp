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

#include "Backup_Fixture.h"

#include "BackupFileSystem_Fake.h"
#include "BackupStrategy_Mock.h"
#include "TestUtils.h"  // IWYU pragma: keep
#include "systools/Backup.h"
#include "systools/BackupStrategy.h"
#include "systools/DirectoryScanner.h"
#include "systools/Path.h"

#include <llamalog/llamalog.h>
#include <m3c/lazy_string.h>

#include <gtest/gtest-spi.h>  // IWYU pragma: keep

#include <detours_gmock.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <new>
#include <string_view>
#include <unordered_map>
#include <utility>


namespace testing {

Message& operator<<(Message& msg, const std::wstring_view& sv) {
	return msg << internal::String::ShowWideCString(sv.data());
}

}  // namespace testing


namespace systools::test {

namespace t = testing;

namespace {

#pragma warning(suppress : 4100)
MATCHER_P2(PathWithExactFilename, path, filename, "") {
	static_assert(std::is_convertible_v<arg_type, const Path&>);
	static_assert(std::is_convertible_v<path_type, const Path&>);
	static_assert(std::is_convertible_v<filename_type, const std::wstring&>);

	return arg.GetParent() == path && arg.GetFilename().sv() == filename;
}

#define WIN32_FUNCTIONS(fn_)                                                                                                                                   \
	fn_(6, BOOL, WINAPI, AdjustTokenPrivileges,                                                                                                                \
		(HANDLE TokenHandle, BOOL DisableAllPrivileges, PTOKEN_PRIVILEGES NewState, DWORD BufferLength, PTOKEN_PRIVILEGES PreviousState, PDWORD ReturnLength), \
		(TokenHandle, DisableAllPrivileges, NewState, BufferLength, PreviousState, ReturnLength),                                                              \
		detours_gmock::SetLastErrorAndReturn(ERROR_SUCCESS, TRUE));

DTGM_DECLARE_API_MOCK(Win32, WIN32_FUNCTIONS);

}  // namespace

// Keep the mock definitions out of the header.
class Backup_Mock {
public:
	void TearDown() {
		t::Mock::VerifyAndClearExpectations(&m_win32);
		DTGM_DETACH_API_MOCK(Win32);
	}

private:
	DTGM_DEFINE_STRICT_API_MOCK(Win32, m_win32);
};

Backup_Fixture::Backup_Fixture(bool enableRootScan)
	: m_src(m_srcVolume)
	, m_ref(m_targetVolume / L"ref")
	, m_dst(m_targetVolume / L"dst")
	, m_root(Root())
	, m_pMock(new t::NiceMock<Backup_Mock>()) {
	m_root.src().ref().dst().enablePathFunctions(true);
	if (enableRootScan) {
		m_root.enableScan();
	}
	m_root.build();
}

Backup_Fixture::~Backup_Fixture() {
	delete m_pMock;
}

void Backup_Fixture::SetUp() {
	ON_CALL(m_strategy, Exists(t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Exists));
	ON_CALL(m_strategy, IsDirectory(t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::IsDirectory));

	ON_CALL(m_strategy, Compare(t::_, t::_, t::_))
		.WillByDefault(t::WithArgs<0, 1>(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Compare)));
	ON_CALL(m_strategy, CreateDirectory(t::_, t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::CreateDirectory));
	ON_CALL(m_strategy, CreateDirectoryRecursive(t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::CreateDirectoryRecursive));
	ON_CALL(m_strategy, SetAttributes(t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::SetAttributes));
	ON_CALL(m_strategy, SetSecurity(t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::SetSecurity));
	ON_CALL(m_strategy, Rename(t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Rename));
	ON_CALL(m_strategy, Copy(t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Copy));
	ON_CALL(m_strategy, CreateHardLink(t::_, t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::CreateHardLink));
	ON_CALL(m_strategy, Delete(t::_))
		.WillByDefault(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Delete));

	ON_CALL(m_strategy, Scan(t::_, t::_, t::_, t::_, t::_, t::_))
		.WillByDefault(t::WithArgs<0, 2, 3, 4, 5>(t::Invoke(&m_fileSystem, &BackupFileSystem_Fake::Scan)));
	EXPECT_CALL(m_strategy, WaitForScan(t::_))
		.Times(t::AnyNumber());
}

void Backup_Fixture::TearDown() {
	m_pMock->TearDown();
}

void Backup_Fixture::VerifyFileSystem(const std::vector<Path>& backupFolders, const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& before, const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& after) const {
	std::vector<Path> dstBackup;
	dstBackup.reserve(backupFolders.size());
	for (const Path& path : backupFolders) {
		dstBackup.push_back(m_dst / path.sv().substr(m_src.size()));
	}

	std::unordered_map<Path, BackupFileSystem_Fake::Entry> srcBefore;
	srcBefore.max_load_factor(0.75f);
	srcBefore.reserve(before.size() / 2);

	std::unordered_map<Path, BackupFileSystem_Fake::Entry> refBefore;
	refBefore.max_load_factor(0.75f);
	refBefore.reserve(before.size() / 2);

	std::unordered_map<Path, BackupFileSystem_Fake::Entry> dstIgnoredBefore;
	dstIgnoredBefore.max_load_factor(0.75f);
	dstIgnoredBefore.reserve(before.size() / 16);

	for (const auto& entry : before) {
		const auto& path = entry.first.sv();
		if (path.starts_with(m_src.sv())) {
			ASSERT_TRUE(srcBefore.insert({entry.first, entry.second}).second) << "Error inserting " << path;
		} else if (path.starts_with(m_ref.sv())) {
			ASSERT_TRUE(refBefore.insert({entry.first, entry.second}).second) << "Error inserting " << path;
		} else if (path.starts_with(m_dst.sv())) {
			if (std::none_of(dstBackup.cbegin(), dstBackup.cend(), [path](const Path& base) {
					return (path.size() == base.size() && base == path)
						   || (path.size() > base.size() && path[base.size()] == L'\\' && base == path.substr(0, base.size()));
				})) {
				ASSERT_TRUE(dstIgnoredBefore.insert({entry.first, entry.second}).second) << "Error inserting " << path;
			}
		}
	}

	std::unordered_map<Path, BackupFileSystem_Fake::Entry> srcCompare(srcBefore);
	std::unordered_map<Path, BackupFileSystem_Fake::Entry> dstAfter;
	dstAfter.max_load_factor(0.75f);
	dstAfter.reserve(after.size() / 2);

	for (const auto& entry : after) {
		const auto& path = entry.first.sv();
		if (path.starts_with(m_src.sv())) {
			auto node = srcBefore.extract(entry.first);
			EXPECT_FALSE(node.empty()) << path.data() << " has been created";
			if (!node.empty()) {
				EXPECT_EQ(node.mapped(), entry.second) << path.data() << " has been altered";
			}
		} else if (path.starts_with(m_ref.sv())) {
			auto node = refBefore.extract(entry.first);
			EXPECT_FALSE(node.empty()) << path.data() << " has been created";
			if (!node.empty()) {
				EXPECT_EQ(node.mapped(), entry.second) << path.data() << " has been altered";
			}
		} else if (path.starts_with(m_dst.sv())) {
			if (std::none_of(dstBackup.cbegin(), dstBackup.cend(), [path](const Path& base) {
					return (path.size() == base.size() && base == path)
						   || (path.size() > base.size() && path[base.size()] == L'\\' && base == path.substr(0, base.size()));
				})) {
				auto node = dstIgnoredBefore.extract(entry.first);
				if (node.empty() && entry.first == m_dst) {
					// creation of dst root path is allowed
					continue;
				}
				EXPECT_FALSE(node.empty()) << path.data() << " has been created";
				if (!node.empty()) {
					EXPECT_EQ(node.mapped(), entry.second) << path.data() << " has been altered";
				}
			} else {
				auto sub = path.substr(m_dst.size());
				const Path srcPath(m_src, sub.data(), sub.size());
				auto node = srcCompare.extract(srcPath);
				EXPECT_FALSE(node.empty()) << path.data() << " was expected to not exist";
				if (!node.empty()) {
					EXPECT_EQ(node.mapped().size, entry.second.size) << path.data() << " mismatch to src";
					EXPECT_EQ(node.mapped().creationTime, entry.second.creationTime) << path.data() << " mismatch to src";
					EXPECT_EQ(node.mapped().lastWriteTime, entry.second.lastWriteTime) << path.data() << " mismatch to src";
					EXPECT_EQ(node.mapped().attributes & BackupStrategy::kCopyAttributeMask, entry.second.attributes & BackupStrategy::kCopyAttributeMask) << path.data() << " mismatch to src";
					EXPECT_EQ(node.mapped().filename, entry.second.filename) << path.data() << " mismatch to src";
					EXPECT_EQ(node.mapped().content, entry.second.content) << path.data() << " mismatch to src";
				}
			}
		}
	}

	EXPECT_THAT(srcBefore, t::IsEmpty()) << "Files have been removed from src";
	EXPECT_THAT(refBefore, t::IsEmpty()) << "Files have been removed from ref";
	EXPECT_THAT(dstIgnoredBefore, t::IsEmpty()) << "Files have been removed from dst";

	// remove any folders which are not part of the backup
	for (auto it = srcCompare.begin(); it != srcCompare.end();) {
		if (std::any_of(backupFolders.cbegin(), backupFolders.cend(), [&it](const Path& path) {
				return Path(it->first.sv().substr(0, path.size())) == Path(path);
			})) {
			++it;
		} else {
			it = srcCompare.erase(it);
		}
	}

	EXPECT_THAT(srcCompare, t::IsEmpty()) << "Files have not been copied";
}

const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& Backup_Fixture::Files() const noexcept {
	return m_fileSystem.GetFiles();
}

Backup::Statistics Backup_Fixture::VerifyBackup(const std::vector<Path>& backupFolders) {
	return RunVerified(backupFolders, [this](const auto& backupFolders) {
		Backup backup(m_strategy);
		return backup.CreateBackup(backupFolders, m_ref, m_dst);
	});
}

//
// FileBuilder
//

FileBuilder::FileBuilder(Backup_Fixture& backupFixture)
	: m_backupFixture(backupFixture)
	, m_root(true)
	, m_srcEntry(Filename(L""), true)
	, m_refEntry(m_srcEntry.CreateCopy())
	, m_dstEntry(m_srcEntry.CreateCopy()) {
}

FileBuilder::FileBuilder(Backup_Fixture& backupFixture, const std::wstring& filename, const bool directory)
	: m_backupFixture(backupFixture)
	, m_srcEntry(Filename(filename), directory)
	, m_refEntry(m_srcEntry.CreateCopy())
	, m_dstEntry(m_srcEntry.CreateCopy()) {
}

FileBuilder& FileBuilder::src() {
	m_src = m_inSrc = true;
	return *this;
}
FileBuilder& FileBuilder::ref() {
	m_ref = m_inRef = true;
	return *this;
}
FileBuilder& FileBuilder::dst() {
	m_dst = m_inDst = true;
	return *this;
}

FileBuilder& FileBuilder::change() {
	m_src = false;
	m_ref = false;
	m_dst = false;
	return *this;
}

FileBuilder& FileBuilder::size(const std::uint64_t value) {
	if (m_src) {
		m_srcEntry.size = value;
	}
	if (m_ref) {
		m_refEntry.size = value;
	}
	if (m_dst) {
		m_dstEntry.size = value;
	}
	return *this;
}

FileBuilder& FileBuilder::creationTime(const std::int64_t value) {
	if (m_src) {
		m_srcEntry.creationTime = value;
	}
	if (m_ref) {
		m_refEntry.creationTime = value;
	}
	if (m_dst) {
		m_dstEntry.creationTime = value;
	}
	return *this;
}

FileBuilder& FileBuilder::lastWriteTime(const std::int64_t value) {
	if (m_src) {
		m_srcEntry.lastWriteTime = value;
	}
	if (m_ref) {
		m_refEntry.lastWriteTime = value;
	}
	if (m_dst) {
		m_dstEntry.lastWriteTime = value;
	}
	return *this;
}

FileBuilder& FileBuilder::attributes(const DWORD value) {
	if (m_src) {
		m_srcEntry.attributes = value | (m_srcEntry.attributes & FILE_ATTRIBUTE_DIRECTORY);
	}
	if (m_ref) {
		m_refEntry.attributes = value | (m_refEntry.attributes & FILE_ATTRIBUTE_DIRECTORY);
	}
	if (m_dst) {
		m_dstEntry.attributes = value | (m_dstEntry.attributes & FILE_ATTRIBUTE_DIRECTORY);
	}
	return *this;
}

FileBuilder& FileBuilder::filename(const std::wstring& value) {
	if (m_src) {
		m_srcEntry.filename = value;
	}
	if (m_ref) {
		m_refEntry.filename = value;
	}
	if (m_dst) {
		m_dstEntry.filename = value;
	}
	return *this;
}

FileBuilder& FileBuilder::content(const std::string& value) {
	if (m_src) {
		m_srcEntry.content = value;
	}
	if (m_ref) {
		m_refEntry.content = value;
	}
	if (m_dst) {
		m_dstEntry.content = value;
	}
	return *this;
}

FileBuilder& FileBuilder::fileId(const std::uint32_t value) {
	if (m_src) {
		m_srcEntry.fileId = value;
	}
	if (m_ref) {
		m_refEntry.fileId = value;
	}
	if (m_dst) {
		m_dstEntry.fileId = value;
	}
	return *this;
}

FileBuilder& FileBuilder::build() {
	if (!m_inSrc && !m_inRef && !m_inDst) {
		THROW(std::exception(), "{} neither exists in source, reference or destination", m_srcEntry.filename);
	}

	const Path mySrcPath = srcPath();
	const Path myRefPath = refPath();
	const Path myDstPath = dstPath();

	if (m_inSrc) {
		m_backupFixture.m_fileSystem.Add(mySrcPath, m_srcEntry, m_root);
	}
	if (m_inRef) {
		m_backupFixture.m_fileSystem.Add(myRefPath, m_refEntry, m_root);
	}
	if (m_inDst) {
		m_backupFixture.m_fileSystem.Add(myDstPath, m_dstEntry, m_root);
	}

	if (m_srcEnablePathFunctions && !m_disableExpect) {
		auto times = m_srcEnablePathFunctions == 1 ? t::Exactly(1) : t::AnyNumber();
		EXPECT_CALL(m_backupFixture.m_strategy, Exists(mySrcPath)).Times(times);
		EXPECT_CALL(m_backupFixture.m_strategy, IsDirectory(mySrcPath)).Times(times);
	}
	if (m_refEnablePathFunctions && !m_disableExpect) {
		auto times = m_refEnablePathFunctions == 1 ? t::Exactly(1) : t::AnyNumber();
		EXPECT_CALL(m_backupFixture.m_strategy, Exists(myRefPath)).Times(times);
		EXPECT_CALL(m_backupFixture.m_strategy, IsDirectory(myRefPath)).Times(times);
	}
	if (m_dstEnablePathFunctions && !m_disableExpect) {
		auto times = m_dstEnablePathFunctions == 1 ? t::Exactly(1) : t::AnyNumber();
		EXPECT_CALL(m_backupFixture.m_strategy, Exists(myDstPath)).Times(times);
		EXPECT_CALL(m_backupFixture.m_strategy, IsDirectory(myDstPath)).Times(times);
	}

	if (m_srcEnableScan) {
		EXPECT_CALL(m_backupFixture.m_strategy, Scan(mySrcPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
	}
	if (m_refEnableScan) {
		EXPECT_CALL(m_backupFixture.m_strategy, Scan(myRefPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
	}
	if (m_dstEnableScan) {
		EXPECT_CALL(m_backupFixture.m_strategy, Scan(myDstPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
	}

	if (m_root || m_disableExpect) {
		return *this;
	}

	// set-up the expectations

	if (m_srcEntry.IsDirectory()) {
		// Scan all src directories
		if (m_inSrc) {
			EXPECT_CALL(m_backupFixture.m_strategy, Scan(mySrcPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
		}

		// Scan ref directories only if src exists
		if (m_inSrc && m_inRef) {
			EXPECT_CALL(m_backupFixture.m_strategy, Scan(myRefPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
		}

		// Scan all dst directories
		if (m_inDst) {
			EXPECT_CALL(m_backupFixture.m_strategy, Scan(myDstPath, t::_, t::_, t::_, t::_, t::_)).RetiresOnSaturation();
		}

		// Create directories missing in dst
		if (m_inSrc && !m_inDst) {
			EXPECT_CALL(m_backupFixture.m_strategy, CreateDirectory(PathWithExactFilename(myDstPath.GetParent(), m_srcEntry.filename), mySrcPath, t::_)).RetiresOnSaturation();
		}

		// rename if src and dst only differ by case
		if (m_inSrc && m_inDst && m_srcEntry.filename != m_dstEntry.filename) {
			EXPECT_CALL(m_backupFixture.m_strategy, Rename(myDstPath, PathWithExactFilename(myDstPath.GetParent(), m_srcEntry.filename))).RetiresOnSaturation();
		}

		// set attributes for all directories in dst that exist in src
		if (m_inSrc) {
			EXPECT_CALL(m_backupFixture.m_strategy, SetAttributes(myDstPath, t::AllOf(
																				 t::Property(&ScannedFile::GetCreationTime, m_srcEntry.creationTime),
																				 t::Property(&ScannedFile::GetLastWriteTime, m_srcEntry.lastWriteTime),
																				 t::Property(&ScannedFile::GetAttributes, m_srcEntry.attributes))))
				.RetiresOnSaturation();
		}

		// delete directories in dst that no longer exist in src
		if (!m_inSrc && m_inDst) {
			EXPECT_CALL(m_backupFixture.m_strategy, Delete(myDstPath)).RetiresOnSaturation();
		}
	} else {
		const bool srcRefSameAttributes = m_inSrc && m_inRef && m_srcEntry.HasSameAttributes(m_refEntry);
		const bool srcDstSameAttributes = m_inSrc && m_inDst && m_srcEntry.HasSameAttributes(m_dstEntry);
		const bool srcRefIdentical = srcRefSameAttributes && m_srcEntry.content == m_refEntry.content;
		const bool srcDstIdentical = srcDstSameAttributes && m_srcEntry.content == m_dstEntry.content;
		const bool refDstSameFile = m_inRef && m_inDst && m_refEntry.fileId == m_dstEntry.fileId;

		// compare files that exist in src and dst if attributes match
		if (m_inSrc && m_inDst && srcDstSameAttributes) {
			EXPECT_CALL(m_backupFixture.m_strategy, Compare(mySrcPath, myDstPath, t::_)).RetiresOnSaturation();
		}

		// rename files that are identical but use different case
		if (m_inSrc && m_inDst && srcDstIdentical && m_srcEntry.filename != m_dstEntry.filename) {
			EXPECT_CALL(m_backupFixture.m_strategy, Rename(myDstPath, PathWithExactFilename(myDstPath.GetParent(), m_srcEntry.filename))).RetiresOnSaturation();
		}

		// delete files from dst which exist in src but are not identical
		if (m_inSrc && m_inDst && !srcDstIdentical) {
			EXPECT_CALL(m_backupFixture.m_strategy, Delete(myDstPath)).RetiresOnSaturation();
		}

		// delete files from dst which do not exist in src
		if (!m_inSrc && m_inDst) {
			EXPECT_CALL(m_backupFixture.m_strategy, Delete(myDstPath)).RetiresOnSaturation();
		}

		// compare all files that exist in src and ref if attributes match but not match in dst was found
		// also skip compare if ref and dst are hard-linked
		if (m_inSrc && m_inRef && srcRefSameAttributes && !srcDstIdentical && !refDstSameFile) {
			EXPECT_CALL(m_backupFixture.m_strategy, Compare(mySrcPath, myRefPath, t::_)).RetiresOnSaturation();
		}

		// create hard link if found in ref but not in dst
		if (m_inSrc && m_inRef && srcRefIdentical && !srcDstIdentical) {
			EXPECT_CALL(m_backupFixture.m_strategy, CreateHardLink(PathWithExactFilename(myDstPath.GetParent(), m_srcEntry.filename), myRefPath)).RetiresOnSaturation();
		}

		if (m_inSrc && !srcRefIdentical && !srcDstIdentical) {
			EXPECT_CALL(m_backupFixture.m_strategy, Copy(mySrcPath, PathWithExactFilename(myDstPath.GetParent(), m_srcEntry.filename))).RetiresOnSaturation();
			EXPECT_CALL(m_backupFixture.m_strategy, SetAttributes(myDstPath, t::AllOf(
																				 t::Property(&ScannedFile::GetCreationTime, m_srcEntry.creationTime),
																				 t::Property(&ScannedFile::GetLastWriteTime, m_srcEntry.lastWriteTime),
																				 t::Property(&ScannedFile::GetAttributes, m_srcEntry.attributes))))
				.RetiresOnSaturation();
		}
	}
	return *this;
}

void FileBuilder::addTo(FileBuilder& parent) {
	if (m_pParent) {
		THROW(std::exception(), "{} already in file system", srcPath());
	};
	m_pParent = &parent;
	build();
}

FileBuilder& FileBuilder::enablePathFunctions(const bool anyNumber) {
	if (m_src) {
		m_srcEnablePathFunctions = anyNumber ? 2 : 1;
	}
	if (m_ref) {
		m_refEnablePathFunctions = anyNumber ? 2 : 1;
	}
	if (m_dst) {
		m_dstEnablePathFunctions = anyNumber ? 2 : 1;
	}
	return *this;
}

FileBuilder& FileBuilder::enableScan() {
	if (m_src) {
		m_srcEnableScan = true;
	}
	if (m_ref) {
		m_refEnableScan = true;
	}
	if (m_dst) {
		m_dstEnableScan = true;
	}
	return *this;
}

FileBuilder& FileBuilder::disableExpect() {
	m_disableExpect = true;
	return *this;
}

FileBuilder& FileBuilder::remove() {
	if (m_src) {
		m_backupFixture.m_fileSystem.Delete(srcPath());
	}
	if (m_ref) {
		m_backupFixture.m_fileSystem.Delete(refPath());
	}
	if (m_dst) {
		m_backupFixture.m_fileSystem.Delete(dstPath());
	}
	return *this;
}

Path FileBuilder::srcPath() const {
	if (m_pParent) {
		return m_pParent->srcPath() / m_srcEntry.filename;
	}
	if (!m_root) {
		THROW(std::exception(), "{} must be root", m_srcEntry.filename);
	}
	return m_backupFixture.m_src;
}

Path FileBuilder::refPath() const {
	if (m_pParent) {
		return m_pParent->refPath() / m_refEntry.filename;
	}
	if (!m_root) {
		THROW(std::exception(), "{} must be root", m_refEntry.filename);
	}
	return m_backupFixture.m_ref;
}

Path FileBuilder::dstPath() const {
	if (m_pParent) {
		return m_pParent->dstPath() / m_dstEntry.filename;
	}
	if (!m_root) {
		THROW(std::exception(), "{} must be root", m_dstEntry.filename);
	}
	return m_backupFixture.m_dst;
}

class Backup_Fixture_Test : public Backup_Fixture {
protected:
	Backup_Fixture_Test()
		: Backup_Fixture(false) {
	}
};

TEST_F(Backup_Fixture_Test, CheckSetup_Basic) {
	m_root.children(
		Folder(L"FolderSrc").disableExpect().src(),
		File(L"FileRef").disableExpect().ref(),
		Folder(L"FolderDst").disableExpect().dst());

	ASSERT_THAT(Files(), t::Contains(t::Key(m_src / L"FolderSrc")));
	ASSERT_THAT(Files(), t::Contains(t::Key(m_ref / L"FileRef")));
	ASSERT_THAT(Files(), t::Contains(t::Key(m_dst / L"FolderDst")));
	EXPECT_TRUE(Files().at(m_src / L"FolderSrc").IsDirectory());
	EXPECT_FALSE(Files().at(m_ref / L"FileRef").IsDirectory());
	EXPECT_TRUE(Files().at(m_dst / L"FolderDst").IsDirectory());
}

TEST_F(Backup_Fixture_Test, CheckSetup_Removed) {
	m_root.children(
		Folder(L"FolderSrc").disableExpect().src(),
		Folder(L"FolderRef").disableExpect().ref(),
		Folder(L"FolderDst").disableExpect().dst());

	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Delete(m_src / L"FolderSrc"); }), "FolderSrc");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Delete(m_ref / L"FolderRef"); }), "FolderRef");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Delete(m_dst / L"FolderDst"); }), "FolderDst");
}

TEST_F(Backup_Fixture_Test, CheckSetup_Created) {
	m_root.children(
		Folder(L"FolderSrc").disableExpect().src(),
		Folder(L"FolderRef").disableExpect().ref(),
		Folder(L"FolderDst").disableExpect().dst());

	// just make something to supply as a security source
	ScannedFile file(Filename(L""), {}, {}, {}, 0, {}, {});
	file.GetSecurity().pSecurityDescriptor.reset(new BackupFileSystem_Fake::security_type(""));

	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this, file](const auto&) { m_fileSystem.CreateDirectory(m_src / L"NewSrc", m_src / L"FolderSrc", file); }), "NewSrc has been created");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this, file](const auto&) { m_fileSystem.CreateDirectory(m_ref / L"NewRef", m_ref / L"FolderRef", file); }), "NewRef has been created");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this, file](const auto&) { m_fileSystem.CreateDirectory(m_dst / L"NewDst", m_dst / L"FolderDst", file); }), "NewDst has been created");
}

TEST_F(Backup_Fixture_Test, CheckSetup_Renamed) {
	m_root.children(
		Folder(L"FolderSrc").disableExpect().src(),
		Folder(L"FolderRef").disableExpect().ref(),
		Folder(L"FolderDst").disableExpect().dst());

	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Rename(m_src / L"FolderSrc", m_src / L"foldersrc"); }), "foldersrc has been altered");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Rename(m_ref / L"FolderRef", m_ref / L"folderref"); }), "folderref has been altered");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) { m_fileSystem.Rename(m_dst / L"FolderDst", m_dst / L"folderdst"); }), "folderdst has been altered");
}

TEST_F(Backup_Fixture_Test, CheckSetup_ChangedAttributes) {
	m_root.children(
		Folder(L"FolderSrc").disableExpect().src(),
		Folder(L"FolderRef").disableExpect().ref(),
		Folder(L"FolderDst").disableExpect().dst());

	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) {
				const ScannedFile scannedFile(Filename(L"FolderSrc"), LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, 0, {},{});
				m_fileSystem.SetAttributes(m_src / L"FolderSrc", scannedFile); }),
							"FolderSrc has been altered");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) {
				const ScannedFile scannedFile(Filename(L"FolderRef"), LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, 0, {},{});
				m_fileSystem.SetAttributes(m_ref / L"FolderRef", scannedFile); }),
							"FolderRef has been altered");
	EXPECT_NONFATAL_FAILURE(RunVerified({}, [this](const auto&) {
				const ScannedFile scannedFile(Filename(L"FolderDst"), LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, LARGE_INTEGER{.QuadPart = 0}, 0, {},{});
				m_fileSystem.SetAttributes(m_dst / L"FolderDst", scannedFile); }),
							"FolderDst has been altered");
}

}  // namespace systools::test
