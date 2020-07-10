#pragma once

#include <m3c/lazy_string.h>
#include <m3c/mutex.h>

#include <systools/Path.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace systools {

class ScannedFile {
public:
	class Stream {
	public:
		using name_type = m3c::lazy_wstring<32>;  ///< @brief name is case sensitive

	public:
		Stream() noexcept = default;
		Stream(name_type&& name, const LARGE_INTEGER size, const ULONG attributes) noexcept;
		Stream(const Stream&) = default;
		Stream(Stream&&) noexcept = default;
		~Stream() noexcept = default;

	public:
		Stream& operator=(const Stream&) = default;
		Stream& operator=(Stream&&) noexcept = default;

		[[nodiscard]] bool operator==(const Stream& oth) const noexcept;

	public:
		[[nodiscard]] const name_type& GetName() const noexcept {
			return m_name;
		}
		[[nodiscard]] std::uint64_t GetSize() const noexcept {
			return m_size;
		}
		[[nodiscard]] std::uint32_t GetAttributes() const noexcept {
			return m_attributes;
		}

	private:
		name_type m_name;
		std::uint64_t m_size;
		std::uint32_t m_attributes;
	};

	struct Security {
	public:
		Security() noexcept = default;
		Security(const Security&) noexcept = default;
		Security(Security&&) noexcept = default;
		~Security() noexcept = default;

		Security& operator=(const Security&) noexcept = default;
		Security& operator=(Security&&) noexcept = default;

		[[nodiscard]] bool operator==(const Security& oth) const;

	public:
		PSID pOwner = nullptr;
		PSID pGroup = nullptr;
		PACL pDacl = nullptr;
		PACL pSacl = nullptr;
		std::shared_ptr<void> pSecurityDescriptor;
	};

public:
	ScannedFile() noexcept = delete;
	ScannedFile(Filename&& name, const LARGE_INTEGER size, const LARGE_INTEGER creationTime, const LARGE_INTEGER lastWriteTime, const ULONG attributes, const FILE_ID_128& fileId, std::vector<Stream>&& streams) noexcept;
	ScannedFile(const ScannedFile&) = default;
	ScannedFile(ScannedFile&&) noexcept = default;
	~ScannedFile() noexcept = default;

public:
	ScannedFile& operator=(const ScannedFile&) = default;
	ScannedFile& operator=(ScannedFile&&) noexcept = default;

public:
	[[nodiscard]] const Filename& GetName() const noexcept {
		return m_name;
	}
	[[nodiscard]] std::uint64_t GetSize() const noexcept {
		return m_size;
	}
	[[nodiscard]] std::int64_t GetCreationTime() const noexcept {
		return m_creationTime;
	}
	[[nodiscard]] std::int64_t GetLastWriteTime() const noexcept {
		return m_lastWriteTime;
	}
	[[nodiscard]] std::uint32_t GetAttributes() const noexcept {
		return m_attributes;
	}
	[[nodiscard]] const FILE_ID_128& GetFileId() const noexcept {
		return m_fileId;
	}
	[[nodiscard]] const std::vector<Stream>& GetStreams() const noexcept {
		return m_streams;
	}
	[[nodiscard]] Security& GetSecurity() noexcept {
		return m_security;
	}
	[[nodiscard]] const Security& GetSecurity() const noexcept {
		return m_security;
	}

	[[nodiscard]] bool IsDirectory() const noexcept {
		return (m_attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
	}

	[[nodiscard]] bool IsHardLink(const ScannedFile& other) const noexcept {
		return std::memcmp(&m_fileId, &other.m_fileId, sizeof(FILE_ID_128)) == 0;
	}


private:
	Filename m_name;
	std::uint64_t m_size;
	std::int64_t m_creationTime;
	std::int64_t m_lastWriteTime;
	std::uint32_t m_attributes;
	FILE_ID_128 m_fileId;
	std::vector<Stream> m_streams;
	Security m_security;
};

class __declspec(novtable) ScannerFilter {
public:
	ScannerFilter() noexcept = default;
	ScannerFilter(const ScannerFilter&) noexcept = default;
	ScannerFilter(ScannerFilter&&) noexcept = default;
	virtual ~ScannerFilter() noexcept = default;

public:
	ScannerFilter& operator=(const ScannerFilter&) noexcept = default;
	ScannerFilter& operator=(ScannerFilter&&) noexcept = default;

public:
	[[nodiscard]] virtual bool Accept(const Filename& name) const = 0;
};

template <typename F>
class LambdaScannerFilter final : public ScannerFilter {
public:
	LambdaScannerFilter(F lambda) noexcept
		: m_lambda(std::move(lambda)) {
		// empty
	}
	LambdaScannerFilter(const LambdaScannerFilter&) noexcept = default;
	LambdaScannerFilter(LambdaScannerFilter&&) noexcept = default;
	virtual ~LambdaScannerFilter() noexcept = default;

public:
	LambdaScannerFilter& operator=(const LambdaScannerFilter&) noexcept = default;
	LambdaScannerFilter& operator=(LambdaScannerFilter&&) noexcept = default;

public:
	[[nodiscard]] bool Accept(const Filename& name) const final {
		return m_lambda(name);
	}

private:
	F m_lambda;
};

static const LambdaScannerFilter kAcceptAllScannerFilter([](const Filename&) noexcept { return true; });

class DirectoryScanner {
public:
	using Result = std::vector<ScannedFile>;
	enum class Flags : std::uint8_t {
		kDefault = 0,
		kFolderSecurity = 1,
		kFileSecurity = 2,
		kFolderStreams = 4
	};

	[[nodiscard]] friend constexpr Flags operator|(const Flags a, const Flags b) noexcept {
		return static_cast<Flags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
	}
	friend constexpr Flags& operator|=(Flags& a, const Flags b) noexcept {
		a = a | b;
		return a;
	}

private:
	struct Context;
	enum class State : std::uint_fast8_t;

public:
	DirectoryScanner();
	DirectoryScanner(const DirectoryScanner&) = delete;
	DirectoryScanner(DirectoryScanner&&) = delete;
	~DirectoryScanner() noexcept;

public:
	DirectoryScanner& operator=(const DirectoryScanner&) = delete;
	DirectoryScanner& operator=(DirectoryScanner&&) = delete;

public:
	void Scan(Path path, Result& directories, Result& files, Flags flags, const ScannerFilter& filter);
	void Wait();

private:
	void Run() noexcept;

private:
	std::unique_ptr<Context> m_pContext;

	m3c::mutex m_mutex;
	m3c::condition_variable m_stateChanged;
	std::atomic<State> m_state;
	std::thread m_thread;
};

}  // namespace systools
