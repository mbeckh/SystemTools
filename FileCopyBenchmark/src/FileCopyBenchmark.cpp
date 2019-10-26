#include <llamalog/LogWriter.h>
#include <llamalog/llamalog.h>
#include <m3c/Handle.h>
#include <m3c/exception.h>
#include <m3c/finally.h>

#include <args.hxx>
#include <windows.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace systools::benchmark {

namespace {

constexpr double kTimestampToMicroseconds = .1;

std::uint64_t GetTimestamp() noexcept {
	// windows timer has maximum resolution of 100ns
	return std::chrono::high_resolution_clock::now().time_since_epoch() / std::chrono::nanoseconds(100);
}

void DoReadWrite(const std::wstring& source, const std::wstring& target) {
	const m3c::Handle hSource = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
	if (!hSource) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", source, lg::LastError());
	}

	const m3c::Handle hTarget = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, hSource);
	if (!hTarget) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", target, lg::LastError());
	}

	DWORD size = 0;
	alignas(4096) std::byte buffer[0x10000];
	while (true) {
		DWORD bytesRead;
		if (!ReadFile(hSource, buffer, sizeof(buffer), &bytesRead, nullptr)) {
			THROW(m3c::windows_exception(GetLastError()), "ReadFile {}", source, lg::LastError());
		}
		if (!bytesRead) {
			break;
		}
		size += bytesRead;

		DWORD bytesWritten;
		DWORD bytesToWrite = bytesRead;
		if (bytesToWrite & 511) {
			bytesToWrite = (bytesToWrite | 511) + 1;
		}
		if (!WriteFile(hTarget, buffer, bytesToWrite, &bytesWritten, nullptr)) {
			THROW(m3c::windows_exception(GetLastError()), "WriteFile {}", target, lg::LastError());
		}
		if (bytesWritten != bytesToWrite) {
			THROW(m3c::windows_exception(ERROR_OPERATION_ABORTED), "WriteFile {} {}/{} bytes", target, bytesWritten, bytesToWrite);
		}
	}

	FILE_END_OF_FILE_INFO eof;
	eof.EndOfFile.QuadPart = size;
	if (!SetFileInformationByHandle(hTarget, FILE_INFO_BY_HANDLE_CLASS::FileEndOfFileInfo, &eof, sizeof(eof))) {
		THROW(m3c::windows_exception(GetLastError()), "SetFileInformationByHandle {} to {} bytes: {}", target, eof.EndOfFile, lg::LastError());
	}
}

void DoReadWriteOverlapped(const std::wstring& source, const std::wstring& target) {
	const m3c::Handle hSource = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr);
	if (!hSource) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", source, lg::LastError());
	}

	const m3c::Handle hTarget = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, hSource);
	if (!hTarget) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", target, lg::LastError());
	}

	constexpr std::uint_fast32_t kBufferSize = 0x10000;
	std::uint_fast8_t readIndex = 0;
	std::uint_fast8_t writeIndex = 0;
	alignas(4096) std::byte buffer0[kBufferSize];
	alignas(4096) std::byte buffer1[kBufferSize];
	std::byte* buffer[2] = {buffer0, buffer1};

	DWORD size = 0;
	OVERLAPPED overlappedRead = {};
	OVERLAPPED overlappedWrite = {};
	overlappedRead.Offset = 0;
	overlappedWrite.Offset = 0;
	DWORD bytesToWrite = 0;
	while (true) {
		if (!ReadFile(hSource, buffer[readIndex], kBufferSize, nullptr, &overlappedRead)) {
			if (GetLastError() == ERROR_HANDLE_EOF) {
				break;
			}
			if (GetLastError() != ERROR_IO_PENDING) {
				THROW(m3c::windows_exception(GetLastError()), "ReadFile {}", source, lg::LastError());
			}
		}

		if (overlappedRead.Offset) {
			DWORD bytesWritten;
			if (!GetOverlappedResult(hTarget, &overlappedWrite, &bytesWritten, TRUE)) {
				THROW(m3c::windows_exception(GetLastError()), "GetOverlappedResult {}", target, lg::LastError());
			}
			if (bytesWritten != bytesToWrite) {
				THROW(m3c::windows_exception(ERROR_OPERATION_ABORTED), "WriteFile {} {}/{} bytes", target, bytesWritten, bytesToWrite);
			}
			overlappedWrite.Offset += bytesWritten;
			writeIndex ^= 1;
		}

		DWORD bytesRead = 0;
		if (!GetOverlappedResult(hSource, &overlappedRead, &bytesRead, TRUE)) {
			if (GetLastError() == ERROR_HANDLE_EOF) {
				break;
			}
		}

		size += bytesRead;
		overlappedRead.Offset += kBufferSize;
		readIndex ^= 1;

		bytesToWrite = bytesRead;
		if (bytesToWrite & 511) {
			bytesToWrite = (bytesToWrite | 511) + 1;
		}
		if (!WriteFile(hTarget, buffer[writeIndex], bytesToWrite, nullptr, &overlappedWrite)) {
			if (GetLastError() != ERROR_IO_PENDING) {
				THROW(m3c::windows_exception(GetLastError()), "WriteFile {}", target, lg::LastError());
			}
		}
	}

	DWORD bytesWritten;
	if (!GetOverlappedResult(hTarget, &overlappedWrite, &bytesWritten, TRUE)) {
		THROW(m3c::windows_exception(GetLastError()), "GetOverlappedResult {}", target, lg::LastError());
	}
	if (bytesWritten != bytesToWrite) {
		THROW(m3c::windows_exception(ERROR_OPERATION_ABORTED), "WriteFile {} {}/{} bytes", target, bytesWritten, bytesToWrite);
	}

	FILE_END_OF_FILE_INFO eof;
	eof.EndOfFile.QuadPart = size;
	if (!SetFileInformationByHandle(hTarget, FILE_INFO_BY_HANDLE_CLASS::FileEndOfFileInfo, &eof, sizeof(eof))) {
		THROW(m3c::windows_exception(GetLastError()), "SetFileInformationByHandle {} to {} bytes: {}", target, eof.EndOfFile, lg::LastError());
	}
}

void DoCopyFile(const std::wstring& source, const std::wstring& target) {
	if (!CopyFileW(source.c_str(), target.c_str(), TRUE)) {
		THROW(m3c::windows_exception(GetLastError()), "CopyFile {} > {}: {}", source, target, lg::LastError());
	}
}

void DoCopyFileEx(const std::wstring& source, const std::wstring& target) {
	BOOL cancel = FALSE;
	if (!CopyFileExW(source.c_str(), target.c_str(), nullptr, nullptr, &cancel, COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING)) {
		THROW(m3c::windows_exception(GetLastError()), "CopyFileEx {} > {}: {}", source, target, lg::LastError());
	}
}

void DoCopyFile2(const std::wstring& source, const std::wstring& target) {
	BOOL cancel = FALSE;

	COPYFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	params.dwCopyFlags = COPY_FILE_FAIL_IF_EXISTS | COPY_FILE_NO_BUFFERING;
	params.pfCancel = &cancel;
	params.pProgressRoutine = nullptr;
	params.pvCallbackContext = nullptr;

	COM_HR(CopyFile2(source.c_str(), target.c_str(), &params), "CopyFile2 {} > {}", source, target);
}


constexpr std::uint_fast32_t kBufferSize = 0x10000 << 1;

struct Context {
	HANDLE hSource;
	std::uint_fast8_t readIndex = 0;
	std::uint_fast8_t writeIndex = 0;
	std::byte* buffer[2];
	SRWLOCK lock[2] = {SRWLOCK_INIT, SRWLOCK_INIT};
	CONDITION_VARIABLE cond[2] = {CONDITION_VARIABLE_INIT, CONDITION_VARIABLE_INIT};
	DWORD bytesRead[2];
	std::atomic_bool valid[2] = {false, false};
	DWORD size = 0;
	DWORD error = 0;
} context;

void Reader(Context* pCtx) {
	Context& ctx = *pCtx;
	while (true) {
		AcquireSRWLockExclusive(&ctx.lock[ctx.readIndex]);
		while (ctx.valid[ctx.readIndex]) {
			SleepConditionVariableSRW(&ctx.cond[ctx.readIndex], &ctx.lock[ctx.readIndex], INFINITE, 0);
		}
		if (!ReadFile(ctx.hSource, ctx.buffer[ctx.readIndex], kBufferSize, &ctx.bytesRead[ctx.readIndex], nullptr)) {
			ctx.error = GetLastError();
			ctx.valid[ctx.readIndex] = true;
			ReleaseSRWLockExclusive(&ctx.lock[ctx.readIndex]);
			WakeAllConditionVariable(&ctx.cond[ctx.readIndex]);
			return;
		}
		if (!ctx.bytesRead[ctx.readIndex]) {
			ctx.valid[ctx.readIndex] = true;
			ReleaseSRWLockExclusive(&ctx.lock[ctx.readIndex]);
			WakeAllConditionVariable(&ctx.cond[ctx.readIndex]);
			return;
		}
		ctx.size += ctx.bytesRead[ctx.readIndex];
		ctx.valid[ctx.readIndex] = true;
		ReleaseSRWLockExclusive(&ctx.lock[ctx.readIndex]);
		WakeAllConditionVariable(&ctx.cond[ctx.readIndex]);
		ctx.readIndex ^= 1;
	}
}

void DoThreads(const std::wstring& source, const std::wstring& target) {
	const m3c::Handle hSource = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
	if (!hSource) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", source, lg::LastError());
	}

	const m3c::Handle hTarget = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, hSource);
	if (!hTarget) {
		THROW(m3c::windows_exception(GetLastError()), "CreateFile {}: {}", target, lg::LastError());
	}

	alignas(4096) std::byte buffer0[kBufferSize];
	alignas(4096) std::byte buffer1[kBufferSize];

	Context ctx;
	ctx.hSource = hSource;
	ctx.buffer[0] = buffer0;
	ctx.buffer[1] = buffer1;

	std::thread thread(Reader, &ctx);
	while (true) {
		AcquireSRWLockExclusive(&ctx.lock[ctx.writeIndex]);
		while (!ctx.valid[ctx.writeIndex]) {
			SleepConditionVariableSRW(&ctx.cond[ctx.writeIndex], &ctx.lock[ctx.writeIndex], INFINITE, 0);
		}

		if (!ctx.bytesRead[ctx.writeIndex] || ctx.error) {
			ReleaseSRWLockExclusive(&ctx.lock[ctx.writeIndex]);
			break;
		}

		DWORD bytesWritten;
		DWORD bytesToWrite = ctx.bytesRead[ctx.writeIndex];
		if (bytesToWrite & 511) {
			bytesToWrite = (bytesToWrite | 511) + 1;
		}
		if (!WriteFile(hTarget, ctx.buffer[ctx.writeIndex], bytesToWrite, &bytesWritten, nullptr)) {
			ReleaseSRWLockExclusive(&ctx.lock[ctx.writeIndex]);
			THROW(m3c::windows_exception(GetLastError()), "WriteFile {}", target, lg::LastError());
		}
		if (bytesWritten != bytesToWrite) {
			ReleaseSRWLockExclusive(&ctx.lock[ctx.writeIndex]);
			THROW(m3c::windows_exception(ERROR_OPERATION_ABORTED), "WriteFile {} {}/{} bytes", target, bytesWritten, bytesToWrite);
		}
		ctx.valid[ctx.writeIndex] = false;
		ReleaseSRWLockExclusive(&ctx.lock[ctx.writeIndex]);
		WakeAllConditionVariable(&ctx.cond[ctx.writeIndex]);
		ctx.writeIndex ^= 1;
	}
	thread.join();
	if (ctx.error) {
		THROW(m3c::windows_exception(ctx.error), "Read error: {}", lg::error_code(ctx.error));
	}

	FILE_END_OF_FILE_INFO eof;
	eof.EndOfFile.QuadPart = ctx.size;
	if (!SetFileInformationByHandle(hTarget, FILE_INFO_BY_HANDLE_CLASS::FileEndOfFileInfo, &eof, sizeof(eof))) {
		THROW(m3c::windows_exception(GetLastError()), "SetFileInformationByHandle {} to {} bytes: {}", target, eof.EndOfFile, lg::LastError());
	}
}

}  // namespace

int RunBenchmark(void (*function)(const std::wstring&, const std::wstring&), const std::string& source, const std::string& target) {
	try {
		std::wstring src = L"\\\\?\\";
		src.append(source.begin(), source.end());

		std::wstring trg = L"\\\\?\\";
		trg.append(target.begin(), target.end());

		DeleteFileW(trg.c_str());

		const std::uint64_t start = GetTimestamp();
		function(src, trg);
		const std::uint64_t end = GetTimestamp();

		printf("%9.2lf s\n", (end - start) * kTimestampToMicroseconds / (std::chrono::seconds(1) / std::chrono::microseconds(1)));
	} catch (...) {
		LOG_FATAL_HRESULT(AS_HRESULT(), "hr={}");
		return 1;
	}
	return 0;
}

}  // namespace systools::benchmark

int main(int argc, char* argv[]) noexcept {
	std::unique_ptr<llamalog::StdErrWriter> writer = std::make_unique<llamalog::StdErrWriter>(llamalog::Priority::kTrace);
	llamalog::Initialize(std::move(writer));
#ifdef _DEBUG
	std::unique_ptr<llamalog::DebugWriter> debugWriter = std::make_unique<llamalog::DebugWriter>(llamalog::Priority::kTrace);
	llamalog::AddWriter(std::move(debugWriter));
#endif

	args::ArgumentParser parser("Benchmark programm for file copy");
	parser.helpParams.programName = argv[0];
	parser.helpParams.proglineCommand = "MODE";

	args::Group mode(parser, "MODE", args::Group::Validators::DontCare, args::Options::Required);
	args::Command rw(mode, "rw", "ReadFile/WriteFile");
	args::Command overlapped(mode, "overlapped", "ReadFile/WriteFile OVERLAPPED");
	args::Command copyFile(mode, "copy", "CopeFile");
	args::Command copyFileEx(mode, "copy_ex", "CopeFileEx");
	args::Command copyFile2(mode, "copy2", "CopeFile2");
	args::Command threads(mode, "threads", "Use threads");

	args::Group options(parser, "OPTIONS", args::Group::Validators::DontCare, args::Options::Global);
	args::HelpFlag help(options, "help", "Display this help menu", {'h', "help"});

	args::Positional<std::string> source(options, "source", "The path of the source file.", "", args::Options::Single | args::Options::Required);
	args::Positional<std::string> target(options, "target", "The path of the target file.", "", args::Options::Single | args::Options::Required);

	try {
		parser.ParseCLI(argc, argv);
	} catch (const args::Completion& e) {
		std::cout << e.what();
		return 0;
	} catch (const args::Help&) {
		std::cout << parser;
		return 0;
	} catch (const args::Error& e) {
		std::cerr << e.what() << std::endl;
		std::cerr << parser;
		return 1;
	}

	if (rw.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoReadWrite, source.Get(), target.Get());
	}
	if (overlapped.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoReadWriteOverlapped, source.Get(), target.Get());
	}
	if (copyFile.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoCopyFile, source.Get(), target.Get());
	}
	if (copyFileEx.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoCopyFileEx, source.Get(), target.Get());
	}
	if (copyFile2.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoCopyFile2, source.Get(), target.Get());
	}
	if (threads.Get()) {
		return systools::benchmark::RunBenchmark(systools::benchmark::DoThreads, source.Get(), target.Get());
	}
}
