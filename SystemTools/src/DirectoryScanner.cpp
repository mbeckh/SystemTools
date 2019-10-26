#include "systools/DirectoryScanner.h"

#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/exception.h>
#include <m3c/finally.h>
#include <m3c/mutex.h>
#include <m3c/types_log.h>

#include <windows.h>

#include <atomic>
#include <cassert>
#include <string>
#include <vector>

namespace systools {

namespace {

#if 0
std::wstring GetFullPath(const m3c::Handle& handle) {
	wchar_t result[MAX_PATH + 1];
	const DWORD len = GetFinalPathNameByHandleW(handle, result, MAX_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError(), "GetFinalPathNameByHandle"));
	}
	if (len <= MAX_PATH) {
		return std::wstring(result, len);
	}

	std::unique_ptr<wchar_t[]> buffer = std::make_unique<wchar_t[]>(len + 1);
	const DWORD ret = GetFinalPathNameByHandleW(handle, buffer.get(), len, FILE_NAME_NORMALIZED | VOLUME_NAME_NT);
	if (ret != len) {
		THROW(m3c::windows_exception(GetLastError(), "GetFinalPathNameByHandle"));
	}

	return std::wstring(buffer.get(), len);
}
#endif
void ScanDirectory(const Path& path, DirectoryScanner::Result& directories, DirectoryScanner::Result& files) {
	const m3c::Handle hDirectory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_READ_DATA | FILE_READ_EA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (!hDirectory) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", path);
	}

	while (true) {
		FILE_ID_EXTD_DIR_INFO dirInfo[64];
		if (!GetFileInformationByHandleEx(hDirectory, FileIdExtdDirectoryInfo, dirInfo, sizeof(dirInfo))) {
			if (const DWORD lastError = GetLastError(); lastError != ERROR_NO_MORE_FILES) {
				THROW(m3c::windows_exception(lastError), "GetFileInformationByHandleEx {}", path);
			}
			return;
		}

		const FILE_ID_EXTD_DIR_INFO* pCurrent = dirInfo;
		while (true) {
			if (pCurrent->FileName[0] == L'.' && (pCurrent->FileNameLength == 2 || (pCurrent->FileNameLength == 4 && pCurrent->FileName[1] == L'.'))) {
				goto next;
			}
			{
				const Filename name(pCurrent->FileName, pCurrent->FileNameLength / sizeof(WCHAR));

				if (pCurrent->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					directories.emplace_back(name, pCurrent->EndOfFile, pCurrent->CreationTime, pCurrent->LastWriteTime, pCurrent->FileAttributes, pCurrent->FileId);
				} else {
					files.emplace_back(name, pCurrent->EndOfFile, pCurrent->CreationTime, pCurrent->LastWriteTime, pCurrent->FileAttributes, pCurrent->FileId);
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

}  // namespace

DirectoryScanner::DirectoryScanner()
	: m_thread(
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

void DirectoryScanner::Scan(const Path& path, Result& directories, Result& files) noexcept {
	assert(m_state.load(std::memory_order_acquire) == State::kIdle);

	Context context = {path, directories, files};
	m_pContext = &context;
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
	if (m_pContext->exceptionPtr) {
		std::rethrow_exception(m_pContext->exceptionPtr);
	}
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
			ScanDirectory(m_pContext->path, m_pContext->directories, m_pContext->files);
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
