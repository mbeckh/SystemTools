#include "systools/DirectoryScanner.h"

namespace systools::test {
#if 0
TEST(DirectoryScanner, Scan) {
	DirectoryScanner scanner;

	DirectoryScanner::Context context;
	DirectoryScanner::Result directories;
	DirectoryScanner::Result files;
	scanner.Scan(context, L"V:\\test", directories, files);
	scanner.Wait();

	for (DirectoryScanner::Entry entry : directories) {
		DirectoryScanner::Result entryDirectories;
		DirectoryScanner::Result entryFiles;
		scanner.Scan(context, entry.fileId, entryDirectories, entryFiles);
		scanner.Wait();
	}
}
#endif
}  // namespace systools::test
