#include "systools/Backup.h"


namespace systools::test {

TEST(Backup, Scan) {
	Backup backup;
	backup.CompareDirectories({"T:\\test"}, "Q:\\", "V:\\");
}

}  // namespace systools::test
