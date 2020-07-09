#pragma once

#include "BackupFileSystem_Fake.h"
#include "BackupStrategy_Mock.h"
#include "TestUtils.h"
#include "systools/Backup.h"
#include "systools/Path.h"

#include <unordered_map>

namespace systools::test {

namespace t = testing;


class Backup_Fixture;

class FileBuilder {
public:
	FileBuilder(Backup_Fixture& backupFixture);
	FileBuilder(Backup_Fixture& backupFixture, const std::wstring& filename, const bool directory);

public:
	FileBuilder& src();
	FileBuilder& ref();
	FileBuilder& dst();
	FileBuilder& change();
	FileBuilder& size(std::uint64_t value);
	FileBuilder& creationTime(std::int64_t value);
	FileBuilder& lastWriteTime(std::int64_t value);
	FileBuilder& attributes(DWORD value);
	FileBuilder& filename(const std::wstring& value);
	FileBuilder& content(const std::string& value);
	FileBuilder& fileId(const std::uint32_t value);

	FileBuilder& build();
	template <typename... F>
	FileBuilder& children(F&&... file) {
		static_assert((std::is_same_v<FileBuilder&, decltype(file)> && ...));
		(file.addTo(*this), ...);
		return *this;
	}
	FileBuilder& enablePathFunctions(bool anyNumber = false);
	FileBuilder& enableScan();
	FileBuilder& disableExpect();

	FileBuilder& remove();

	Path srcPath() const;
	Path refPath() const;
	Path dstPath() const;

	const BackupFileSystem_Fake::Entry& srcEntry() {
		return m_srcEntry;
	}
	const BackupFileSystem_Fake::Entry& refEntry() {
		return m_refEntry;
	}
	const BackupFileSystem_Fake::Entry& dstEntry() {
		return m_dstEntry;
	}

private:
	void addTo(FileBuilder& parent);

private:
	Backup_Fixture& m_backupFixture;
	FileBuilder* m_pParent = nullptr;
	bool m_root = false;

	bool m_inSrc = false;
	bool m_inRef = false;
	bool m_inDst = false;

	bool m_src = false;
	bool m_ref = false;
	bool m_dst = false;

	BackupFileSystem_Fake::Entry m_srcEntry;
	BackupFileSystem_Fake::Entry m_refEntry;
	BackupFileSystem_Fake::Entry m_dstEntry;

	std::uint8_t m_srcEnablePathFunctions = 0;
	std::uint8_t m_refEnablePathFunctions = 0;
	std::uint8_t m_dstEnablePathFunctions = 0;
	bool m_srcEnableScan = false;
	bool m_refEnableScan = false;
	bool m_dstEnableScan = false;

	bool m_disableExpect = false;
};

class Backup_Mock;

class Backup_Fixture : public t::Test {
protected:
	Backup_Fixture(bool enableRootScan = true);
	~Backup_Fixture();

protected:
	void SetUp() override;
	void TearDown() override;

protected:
	FileBuilder Root() {
		return FileBuilder(*this);
	}

	FileBuilder Folder(const std::wstring& name) {
		return FileBuilder(*this, name, true);
	}

	FileBuilder File(const std::wstring& name) {
		return FileBuilder(*this, name, false);
	}

	template <typename Action>
	Backup::Statistics RunVerified(const std::vector<Path, std::allocator<Path>>& backupFolders, Action action) {
		const std::unordered_map<Path, BackupFileSystem_Fake::Entry> before = Files();

		Backup::Statistics result;
		if constexpr (std::is_same_v<std::invoke_result_t<Action, decltype(backupFolders)>, void>) {
			action(backupFolders);
		} else {
			result = action(backupFolders);
		}

		const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& after = Files();
		VerifyFileSystem(backupFolders, before, after);

		return result;
	}

	virtual Backup::Statistics VerifyBackup(const std::vector<Path>& backupFolders);

	const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& Files() const noexcept;

private:
	void VerifyFileSystem(const std::vector<Path>& backupFolders, const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& before, const std::unordered_map<Path, BackupFileSystem_Fake::Entry>& after) const;

protected:
	const Path m_srcVolume = Path(LR"(\\?\Volume{00112233-4455-6677-8899-AABBCCDDEEF0}\)");
	const Path m_targetVolume = Path(LR"(\\?\Volume{00112233-4455-6677-8899-AABBCCDDEEFA}\)");

protected:
	std::atomic_uint32_t m_openHandles = 0;
	t::StrictMock<BackupStrategy_Mock> m_strategy;
	BackupFileSystem_Fake m_fileSystem;

	Path m_src;
	Path m_ref;
	Path m_dst;
	FileBuilder m_root;

private:
	Backup_Mock* m_pMock;

	friend FileBuilder;
};

}  // namespace systools::test
