#include "systools/DirectoryScanner.h"

#include <llamalog/llamalog.h>
#include <m3c/exception.h>
#include <m3c/finally.h>
#include <m3c/handle.h>
#include <m3c/mutex.h>
#include <m3c/types_log.h>

#include <accctrl.h>
#include <aclapi.h>
#include <windows.h>

#include <atomic>
#include <cassert>
#include <string>
#include <vector>

namespace systools {

namespace {

const auto LocalFreeDelete = [](void* ptr) noexcept {
	if (LocalFree(ptr)) {
		SLOG_ERROR("LocalFree: {}", lg::LastError());
	}
};

constexpr [[nodiscard]] bool operator<(const DirectoryScanner::Flags test, const DirectoryScanner::Flags value) noexcept {
	return (static_cast<std::uint8_t>(test) & static_cast<std::uint8_t>(value)) == static_cast<std::uint8_t>(test);
}

void ScanDirectory(const Path& path, DirectoryScanner::Result& directories, DirectoryScanner::Result& files, const DirectoryScanner::Flags flags, const ScannerFilter& filter) {
	const m3c::handle hDirectory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_DATA | FILE_READ_EA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!hDirectory) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", path);
	}

	while (true) {
		constexpr std::size_t kSize = 0x40000;  // 256 KB
		std::unique_ptr<std::byte[]> dirInfo = std::make_unique<std::byte[]>(kSize);
		if (!GetFileInformationByHandleEx(hDirectory, FileIdExtdDirectoryInfo, dirInfo.get(), kSize)) {
			if (const DWORD lastError = GetLastError(); lastError != ERROR_NO_MORE_FILES) {
				THROW(m3c::windows_exception(lastError), "GetFileInformationByHandleEx {}", path);
			}
			return;
		}

		const FILE_ID_EXTD_DIR_INFO* pCurrent = reinterpret_cast<FILE_ID_EXTD_DIR_INFO*>(dirInfo.get());
		while (true) {
			if (pCurrent->FileName[0] == L'.' && (pCurrent->FileNameLength == 2 || (pCurrent->FileNameLength == 4 && pCurrent->FileName[1] == L'.'))) {
				goto next;
			}

			// block required because of goto
			{
				Filename name(pCurrent->FileName, pCurrent->FileNameLength / sizeof(WCHAR));
				const Path filePath = path / name;
				const bool directory = pCurrent->FileAttributes & FILE_ATTRIBUTE_DIRECTORY;

				if (!filter.Accept(name)) {
					goto next;
				}

				std::vector<ScannedFile::Stream> streams;
				// get streams
				if (!directory || DirectoryScanner::Flags::kFolderStreams < flags) {
					WIN32_FIND_STREAM_DATA stream;
					const m3c::find_handle hFind = FindFirstStreamW(filePath.c_str(), FindStreamInfoStandard, &stream, 0);
					if (!hFind) {
						if (const DWORD lastError = GetLastError(); lastError != ERROR_HANDLE_EOF) {
							THROW(m3c::windows_exception(lastError), "FindFirstStreamW {}", filePath);
						}
					} else {
						// do process first stream for directories
						if (directory) {
							// jump into loop, bypassing FindNextStreamW once
							goto findLoop;
						}
						while (FindNextStreamW(hFind, &stream)) {
						findLoop:
							assert(stream.cStreamName[1] != L':');

							ScannedFile::Stream::name_type streamName(stream.cStreamName);
							const Path streamPath = filePath + streamName;
							const DWORD streamAttributes = GetFileAttributesW(streamPath.c_str());
							if (streamAttributes == INVALID_FILE_ATTRIBUTES) {
								THROW(m3c::windows_exception(GetLastError()), "GetFileAttributesW {}", streamPath);
							}
							streams.emplace_back(std::move(streamName), stream.StreamSize, streamAttributes);
						}
						if (const DWORD lastError = GetLastError(); lastError != ERROR_HANDLE_EOF) {
							THROW(m3c::windows_exception(lastError), "FindNextStreamW {}", filePath);
						}
					}
				}

				ScannedFile scannedFile(std::move(name), pCurrent->EndOfFile, pCurrent->CreationTime, pCurrent->LastWriteTime, pCurrent->FileAttributes, pCurrent->FileId, std::move(streams));

				// get security info _after_ filter
				if ((directory && DirectoryScanner::Flags::kFolderSecurity < flags) || (!directory && DirectoryScanner::Flags::kFileSecurity < flags)) {
					constexpr SECURITY_INFORMATION kSecurityInformation = ATTRIBUTE_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION;
					ScannedFile::Security& security = scannedFile.GetSecurity();
					PSECURITY_DESCRIPTOR pSecurityDescriptor;
					const DWORD result = GetNamedSecurityInfoW(filePath.c_str(), SE_FILE_OBJECT, kSecurityInformation, &security.pOwner, &security.pGroup, &security.pDacl, &security.pSacl, &pSecurityDescriptor);
					if (result != ERROR_SUCCESS) {
						THROW(m3c::windows_exception(result), "GetNamedSecurityInfoW {}", filePath);
					}
					security.pSecurityDescriptor.reset(pSecurityDescriptor, LocalFreeDelete);
				}

				if (directory) {
					directories.push_back(std::move(scannedFile));
				} else {
					files.push_back(std::move(scannedFile));
				}
			}
		next:
			if (!pCurrent->NextEntryOffset) {
				break;
			}
			pCurrent = reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(reinterpret_cast<std::uintptr_t>(pCurrent) + pCurrent->NextEntryOffset);
		}
	}
}

[[nodiscard]] bool EqualTrustee(const TRUSTEE_W& lhs, const TRUSTEE_W& rhs) {
	if (lhs.pMultipleTrustee || rhs.pMultipleTrustee) {
		THROW(std::domain_error("pMultipleTrustee is not supported"));
	}
	if (lhs.MultipleTrusteeOperation != NO_MULTIPLE_TRUSTEE || rhs.MultipleTrusteeOperation != NO_MULTIPLE_TRUSTEE) {
		THROW(std::domain_error("MultipleTrusteeOperation is not supported"));
	}

	if (lhs.TrusteeForm != rhs.TrusteeForm || lhs.TrusteeType != rhs.TrusteeType) {
		return false;
	}

	switch (lhs.TrusteeForm) {
	case TRUSTEE_IS_SID:
		return EqualSid(lhs.ptstrName, rhs.ptstrName);
	case TRUSTEE_IS_NAME:
		return std::wcscmp(lhs.ptstrName, rhs.ptstrName) == 0;
	case TRUSTEE_IS_OBJECTS_AND_SID: {
		const OBJECTS_AND_SID& lhsOas = reinterpret_cast<const OBJECTS_AND_SID&>(lhs.ptstrName);
		const OBJECTS_AND_SID& rhsOas = reinterpret_cast<const OBJECTS_AND_SID&>(rhs.ptstrName);
		if (lhsOas.ObjectsPresent != rhsOas.ObjectsPresent || !EqualSid(lhsOas.pSid, rhsOas.pSid)) {
			return false;
		}
		if ((lhsOas.ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) && !IsEqualGUID(lhsOas.ObjectTypeGuid, rhsOas.ObjectTypeGuid)) {
			return false;
		}
		if ((lhsOas.ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) && !IsEqualGUID(lhsOas.InheritedObjectTypeGuid, rhsOas.InheritedObjectTypeGuid)) {
			return false;
		}
		return true;
	}
	case TRUSTEE_IS_OBJECTS_AND_NAME: {
		const OBJECTS_AND_NAME_W& lhsOas = reinterpret_cast<const OBJECTS_AND_NAME_W&>(lhs.ptstrName);
		const OBJECTS_AND_NAME_W& rhsOas = reinterpret_cast<const OBJECTS_AND_NAME_W&>(rhs.ptstrName);
		if (lhsOas.ObjectsPresent != rhsOas.ObjectsPresent || lhsOas.ObjectType != rhsOas.ObjectType || std::wcscmp(lhsOas.ptstrName, rhsOas.ptstrName)) {
			return false;
		}
		if ((lhsOas.ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) && std::wcscmp(lhsOas.ObjectTypeName, rhsOas.ObjectTypeName)) {
			return false;
		}
		if ((lhsOas.ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) && std::wcscmp(lhsOas.InheritedObjectTypeName, rhsOas.InheritedObjectTypeName)) {
			return false;
		}
		return true;
	}
	}
	assert(false);
	return true;
}

[[nodiscard]] bool EqualExplicitAccess(const EXPLICIT_ACCESS_W& lhs, const EXPLICIT_ACCESS_W& rhs) {
	return lhs.grfAccessPermissions == rhs.grfAccessPermissions
		   && lhs.grfAccessMode == rhs.grfAccessMode
		   && lhs.grfInheritance == rhs.grfInheritance
		   && EqualTrustee(lhs.Trustee, rhs.Trustee);
}

[[nodiscard]] bool EqualAcl(const PACL pLhs, const PACL pRhs) {
	PEXPLICIT_ACCESS_W ptr;

	ULONG lhsEntries;
	const DWORD lhsResult = GetExplicitEntriesFromAclW(pLhs, &lhsEntries, &ptr);
	if (lhsResult != ERROR_SUCCESS) {
		THROW(m3c::windows_exception(lhsResult, "GetExplicitEntriesFromAclW"));
	}
	const std::unique_ptr<EXPLICIT_ACCESSW[], decltype(LocalFreeDelete)> pLhsAccess(ptr);

	ULONG rhsEntries;
	const DWORD rhsResult = GetExplicitEntriesFromAclW(pRhs, &rhsEntries, &ptr);
	if (rhsResult != ERROR_SUCCESS) {
		THROW(m3c::windows_exception(rhsResult, "GetExplicitEntriesFromAclW"));
	}
	const std::unique_ptr<EXPLICIT_ACCESSW[], decltype(LocalFreeDelete)> pRhsAccess(ptr);

	if (lhsEntries != rhsEntries) {
		return false;
	}

	for (ULONG i = 0; i < lhsEntries; ++i) {
		if (!EqualExplicitAccess(pLhsAccess[i], pRhsAccess[i])) {
			return false;
		}
	}
	return true;
}

}  // namespace


//
// ScannedFile
//

ScannedFile::ScannedFile(Filename&& name, const LARGE_INTEGER size, const LARGE_INTEGER creationTime, const LARGE_INTEGER lastWriteTime, const ULONG attributes, const FILE_ID_128& fileId, std::vector<Stream>&& streams) noexcept
	: m_name(std::move(name))
	, m_size(size.QuadPart)
	, m_creationTime(creationTime.QuadPart)
	, m_lastWriteTime(lastWriteTime.QuadPart)
	, m_attributes(attributes)
	, m_fileId(fileId)
	, m_streams(std::move(streams)) {
	assert(size.QuadPart >= 0);
	std::sort(
		m_streams.begin(), m_streams.end(), [](const Stream& lhs, const Stream& rhs) noexcept {
			return lhs.GetName() < rhs.GetName();
		});
}

//
// ScannedFile::Stream
//

ScannedFile::Stream::Stream(name_type&& name, const LARGE_INTEGER size, const ULONG attributes) noexcept
	: m_name(std::move(name))
	, m_size(size.QuadPart)
	, m_attributes(attributes) {
	assert(size.QuadPart >= 0);
}

[[nodiscard]] bool ScannedFile::Stream::operator==(const Stream& oth) const noexcept {
	return m_name == oth.m_name
		   && m_size == oth.m_size
		   && m_attributes == oth.m_attributes;
}


//
// ScannedFile::Security
//

[[nodiscard]] bool ScannedFile::Security::operator==(const ScannedFile::Security& oth) const {
	return ((pOwner && oth.pOwner && EqualSid(pOwner, oth.pOwner)) || (!pOwner && !oth.pOwner))
		   && ((pGroup && oth.pGroup && EqualSid(pGroup, oth.pGroup)) || (!pGroup && !oth.pGroup))
		   && ((pDacl && oth.pDacl && EqualAcl(pDacl, oth.pDacl)) || (!pDacl && !oth.pDacl))
		   && ((pSacl && oth.pSacl && EqualAcl(pSacl, oth.pSacl)) || (!pSacl && !oth.pSacl));
}


//
// DirectoryScanner::Context
//

struct DirectoryScanner::Context final {
	Context(Path& path, Result& directories, Result& files, const Flags flags, const ScannerFilter& filter) noexcept
		: path(std::move(path))
		, directories(directories)
		, files(files)
		, flags(flags)
		, filter(filter) {
		// empty
	}
	Context(const Context&) = delete;
	Context(Context&&) = delete;
	~Context() noexcept = default;

	Context& operator=(const Context&) = delete;
	Context& operator=(Context&&) = delete;

	const Path path;
	Result& directories;
	Result& files;
	const Flags flags;
	const ScannerFilter& filter;
	std::exception_ptr exceptionPtr;
};

//
// DirectoryScanner::State
//

enum class DirectoryScanner::State : std::uint_fast8_t { kIdle = 1,
														 kRunning = 2,
														 kShutdown = 4 };


//
// DirectoryScanner
//

DirectoryScanner::DirectoryScanner()
	: m_state(State::kIdle)
	, m_thread(
		  [](DirectoryScanner* const pScanner) noexcept {
			  pScanner->Run();
		  },
		  this) {
}

DirectoryScanner::~DirectoryScanner() noexcept {
	{
		m3c::scoped_lock lock(m_mutex);
		m_state.store(State::kShutdown, std::memory_order_release);
	}

	m_stateChanged.notify_one();
	try {
		m_thread.join();
	} catch (const std::exception& e) {
		LOG_ERROR("thread.join: {}", e);
	}
}

void DirectoryScanner::Scan(Path path, Result& directories, Result& files, const Flags flags, const ScannerFilter& filter) {
	assert(m_state.load(std::memory_order_acquire) == State::kIdle);

	m_pContext = std::make_unique<Context>(path, directories, files, flags, filter);
	std::atomic_thread_fence(std::memory_order_release);

	{
		m3c::scoped_lock lock(m_mutex);
		m_state.store(State::kRunning, std::memory_order_release);
	}
	m_stateChanged.notify_one();
}

void DirectoryScanner::Wait() {
	State state;
	{
		m3c::shared_lock lock(m_mutex);
		while ((state = m_state.load(std::memory_order_acquire)) == State::kRunning) {
			m_stateChanged.wait(lock);
		}
	}

	if (state == State::kShutdown) {
		THROW(std::exception("wait aborted"));
	}

	std::atomic_thread_fence(std::memory_order_acquire);
	if (m_pContext && m_pContext->exceptionPtr) {
		std::exception_ptr exceptionPtr = m_pContext->exceptionPtr;
		m_pContext.reset();
		std::rethrow_exception(exceptionPtr);
	}
	m_pContext.reset();
}

void DirectoryScanner::Run() noexcept {
	while (true) {
		State state;
		{
			m3c::shared_lock lock(m_mutex);
			while ((state = m_state.load(std::memory_order_acquire)) == State::kIdle) {
				m_stateChanged.wait(lock);
			}
		}

		if (state == State::kShutdown) {
			return;
		}

		std::atomic_thread_fence(std::memory_order_acquire);
		try {
			ScanDirectory(m_pContext->path, m_pContext->directories, m_pContext->files, m_pContext->flags, m_pContext->filter);
		} catch (...) {
			m_pContext->exceptionPtr = std::current_exception();
		}

		std::atomic_thread_fence(std::memory_order_release);
		{
			m3c::scoped_lock lock(m_mutex);
			m_state.store(State::kIdle, std::memory_order_release);
		}

		m_stateChanged.notify_one();
	}
}

}  // namespace systools
