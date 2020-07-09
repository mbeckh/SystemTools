#pragma once

#include <systools/DirectoryScanner.h>
#include <systools/FileComparer.h>

#include <optional>
#include <vector>

namespace systools {

class Path;
class BackupStrategy;

class Backup final {
private:
	struct Match;

public:
	class Statistics {
	public:
		class Entry {
		public:
			[[nodiscard]] std::uint64_t GetFolders() const noexcept {
				return m_folders;
			}
			[[nodiscard]] std::uint64_t GetFiles() const noexcept {
				return m_files;
			}
			[[nodiscard]] std::uint64_t GetSize() const noexcept {
				return m_size;
			}

		private:
			std::uint64_t m_folders = 0;
			std::uint64_t m_files = 0;
			std::uint64_t m_size = 0;

			friend class Statistics;
		};

	public:
		[[nodiscard]] std::uint64_t GetFolders() const noexcept;
		[[nodiscard]] std::uint64_t GetFiles() const noexcept;

		[[nodiscard]] std::uint64_t GetBytesTotal() const noexcept;
		[[nodiscard]] std::uint64_t GetBytesInHardLinks() const noexcept;

		[[nodiscard]] std::uint64_t GetBytesCopied() const noexcept;
		[[nodiscard]] std::uint64_t GetBytesCreatedInHardLinks() const noexcept;

		[[nodiscard]] const Entry& GetAdded() const noexcept {
			return m_added;
		}

		[[nodiscard]] const Entry& GetUpdated() const noexcept {
			return m_updated;
		}

		[[nodiscard]] const Entry& GetRetained() const noexcept {
			return m_retained;
		}

		[[nodiscard]] const Entry& GetRemoved() const noexcept {
			return m_removed;
		}

		[[nodiscard]] const Entry& GetReplaced() const noexcept {
			return m_replaced;
		}

	private:
		void OnAdd(const Match& match);
		void OnUpdate(const Match& match);
		void OnRetain(const Match& match);
		void OnRemove(const Match& match);

		void OnReplace(const Match& match);
		void OnSecurityUpdate(const Match& match);

		void OnCopy(std::uint64_t bytes);
		void OnHardLink(std::uint64_t bytes);

	private:
		void OnEvent(Entry& entry, const ScannedFile& file);

	private:
		Entry m_added;
		Entry m_updated;
		Entry m_retained;
		Entry m_removed;

		Entry m_replaced;
		Entry m_securityUpdated;

		std::uint64_t m_bytesInHardLinks = 0;

		std::uint64_t m_bytesCreatedInHardLinks = 0;
		std::uint64_t m_bytesCopied = 0;

		friend class Backup;
	};

public:
	explicit Backup(BackupStrategy& strategy) noexcept;
	Backup(const Backup&) = delete;
	Backup(Backup&&) = delete;
	~Backup() noexcept = default;

public:
	Backup& operator=(const Backup&) = delete;
	Backup& operator=(Backup&&) = delete;


public:
	Statistics CreateBackup(const std::vector<Path>& src, const Path& ref, const Path& dst);

private:
	void CopyDirectories(const std::optional<Path>& optionalSrc, const std::optional<Path>& optionalRef, const Path& dst, const std::vector<Match>& directories);

private:
	BackupStrategy& m_strategy;
	DirectoryScanner m_srcScanner;
	DirectoryScanner m_refScanner;
	DirectoryScanner m_dstScanner;
	FileComparer m_fileComparer;

	Statistics m_statistics;
	bool m_compareContents = true;
	bool m_fileSecurity = true;
};

}  // namespace systools
