#include "systools/Volume.h"

#include "systools/Path.h"

#include <llamalog/llamalog.h>
#include <m3c/exception.h>
#include <m3c/handle.h>

#include <fmt/format.h>

#include <windows.h>
#include <winioctl.h>

#include <numeric>

namespace systools {

namespace {

void StripToVolumeName(Volume::string_type& path) {
	wchar_t volumePath[MAX_PATH];
	if (!GetVolumePathNameW(path.c_str(), volumePath, sizeof(volumePath) / sizeof(volumePath[0]))) {
		THROW(m3c::windows_exception(GetLastError()), "GetVolumePathName {}", path);
	}

	wchar_t volumeName[50];
	if (!GetVolumeNameForVolumeMountPointW(volumePath, volumeName, sizeof(volumeName) / sizeof(volumeName[0]))) {
		THROW(m3c::windows_exception(GetLastError()), "GetVolumeNameForVolumeMountPoint {}", volumePath);
	}

	path = volumeName;
	if (path.sv().back() == L'\\') {
		path.resize(path.size() - 1);
	}
}

}  // namespace

Volume::Volume(const Path& path)
	: m_name(path.sv()) {
}

std::uint32_t Volume::GetUnbufferedFileOffsetAlignment() {
	if (!m_unbufferedFileOffsetAlignment) {
		ReadUnbufferedAlignments();
	}
	return m_unbufferedFileOffsetAlignment;
}

std::align_val_t Volume::GetUnbufferedMemoryAlignment() {
	if (m_unbufferedMemoryAlignment == static_cast<std::align_val_t>(0)) {
		ReadUnbufferedAlignments();
	}
	return m_unbufferedMemoryAlignment;
}


void Volume::ReadUnbufferedAlignments() {
	StripToVolumeName(m_name);

	const m3c::handle hVolume = CreateFileW(m_name.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (!hVolume) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", m_name);
	}

	VOLUME_DISK_EXTENTS volumeDiskExtents;
	DWORD bytesReturned;
	std::unique_ptr<std::byte[]> buffer;
	const VOLUME_DISK_EXTENTS* pVolumeDiskExtents = &volumeDiskExtents;

	if (!DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, &volumeDiskExtents, sizeof(volumeDiskExtents), &bytesReturned, nullptr)) {
		if (GetLastError() == ERROR_MORE_DATA) {
			const std::size_t size = sizeof(VOLUME_DISK_EXTENTS) + (volumeDiskExtents.NumberOfDiskExtents - 1) * sizeof(volumeDiskExtents.Extents);
			buffer = std::make_unique<std::byte[]>(size);
			if (!DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buffer.get(), static_cast<DWORD>(size), &bytesReturned, nullptr)) {
				THROW(m3c::windows_exception(GetLastError()), "DeviceIoControl {}", m_name);
			}
			pVolumeDiskExtents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buffer.get());
		} else {
			THROW(m3c::windows_exception(GetLastError()), "DeviceIoControl {}", m_name);
		}
	}

	DWORD unbufferedFileOffsetAlignment = 1;
	DWORD unbufferedMemoryAlignment = 1;
	for (DWORD i = 0; i < pVolumeDiskExtents->NumberOfDiskExtents; ++i) {
		const std::wstring deviceName = fmt::format(LR"(\\.\PhysicalDrive{})", pVolumeDiskExtents->Extents[i].DiskNumber);
		const m3c::handle hDevice = CreateFileW(deviceName.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (!hDevice) {
			THROW(m3c::windows_exception(GetLastError()), "CreateFile {}", deviceName);
		}

		STORAGE_PROPERTY_QUERY query = {STORAGE_PROPERTY_ID::StorageAccessAlignmentProperty, STORAGE_QUERY_TYPE::PropertyStandardQuery};

		STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignment = {sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR), sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR)};
		if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &alignment, sizeof(alignment), &bytesReturned, nullptr)) {
			THROW(m3c::windows_exception(GetLastError()), "DeviceIoControl {}", deviceName);
		}

		unbufferedFileOffsetAlignment = std::lcm(unbufferedFileOffsetAlignment, alignment.BytesPerLogicalSector);
		unbufferedMemoryAlignment = std::lcm(unbufferedMemoryAlignment, alignment.BytesPerPhysicalSector);
	}

	m_unbufferedFileOffsetAlignment = unbufferedFileOffsetAlignment;
	m_unbufferedMemoryAlignment = static_cast<std::align_val_t>(unbufferedMemoryAlignment);
}

}  // namespace systools
