#include "TestUtils.h"

#include <m3c/exception.h>

#include <windows.h>

namespace systools::test {

Path TestUtils::GetSystemDirectory() {
	std::wstring systemDirectory;
	const UINT len = ::GetSystemDirectoryW(systemDirectory.data(), 0);
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetSystemDirectory");
	}
	systemDirectory.resize(len);
	const UINT result = ::GetSystemDirectoryW(systemDirectory.data(), len);
	if (!result || result >= len) {
		THROW(m3c::windows_exception(GetLastError()), "GetSystemDirectory");
	}
	systemDirectory.resize(result);
	return Path(systemDirectory);
}

Path TestUtils::GetTempDirectory() {
	std::wstring tempDirectory;
	const DWORD len = ::GetTempPathW(0, tempDirectory.data());
	if (!len) {
		THROW(m3c::windows_exception(GetLastError()), "GetTempPath");
	}
	tempDirectory.resize(len);
	const DWORD result = ::GetTempPathW(len, tempDirectory.data());
	if (!result || result >= len) {
		THROW(m3c::windows_exception(GetLastError()), "GetTempPath");
	}
	tempDirectory.resize(result);
	return Path(tempDirectory);
}

}  // namespace systools::test
