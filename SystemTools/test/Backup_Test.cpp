#include "systools/Backup.h"

#include "Backup_Fixture.h"
#include "TestUtils.h"

/// @file
/// @brief Test cases for the `systools::Backup` class.
/// @details The following names are used in the tests.
///
/// - ...ed: State after action
/// - Stale: dst exists but is out of sync with src
/// - Tweaked: dst is in sync with ref but out of sync with src
///
/// Name          | src   | ref   | dst            | Action
/// ---------------------------------------------------------------------------------
/// Add           | yes   | no    | no             | copy src to dst
/// Added         | yes   | no    | ==src          | nothing
/// StaleAdded    | yes   | no    | <>src          | remove dst, copy src to dst
/// Retain        | yes   | ==src | no             | hard link dst to ref
/// Retained      | yes   | ==src | ==src && ==ref | nothing
/// StaleRetained | yes   | ==src | <>src && <>ref | remove dst, hard link dst to ref
/// Update        | yes   | <>src | no             | copy src to dst
/// Updated       | yes   | <>src | ==src && <>ref | nothing
/// StaleUpdated  | yes   | <>src | <>src && <>ref | remove dst, copy src to dst
/// Tweaked       | yes   | <>src | <>src && ==ref | remove dst, copy src to dst
/// Remove        | no    | yes   | ==ref          | remove dst
/// StaleRemove   | no    | yes   | <>ref          | remove dst
/// Removed       | no    | yes   | no             | nothing
/// Extra         | no    | no    | yes            | remove dst
///
/// Ignored Folders (Root only)
/// ---------------------------------------------------------------------------------
/// Unprocessed: exists in src and not part of backup
/// Ignored: exists in dst and not part of backup

namespace systools::test {

namespace t = testing;

namespace {

constexpr std::int64_t kMagicCreationTime = 0x1'0000;
constexpr std::int64_t kMagicLastWriteTime = 0x2'0000;
constexpr DWORD kMagicAttribute = FILE_ATTRIBUTE_NORMAL;
constexpr std::uint64_t kMagicFileSize = 0x4000;
constexpr char kMagicContentSuffix[] = "*";
constexpr std::uint64_t kMagicFileId = 0x8'0000;

// assert that attribute is actually used by backup
static_assert(BackupStrategy::kCopyAttributeMask & kMagicAttribute);

#pragma warning(suppress : 4100)
MATCHER_P(HasAttributes, attributes, "") {
	return (arg & BackupStrategy::kCopyAttributeMask) == attributes;
}

enum class Mode : std::uint8_t {
	kFolder,
	kFileCopy,     ///< @brief File in dst is a copy of ref.
	kFileHardLink  ///< @brief File in dst is a hard link to ref.
};

void PrintTo(const Mode mode, std::ostream* const os) {
	switch (mode) {
	case Mode::kFolder:
		*os << "Folder";
		break;
	case Mode::kFileCopy:
		*os << "File";
		break;
	case Mode::kFileHardLink:
		*os << "HardLink";
		break;
	}
}

enum class Location : std::uint32_t {
	// using /* */ to add padding for alignment
	kNone = 0,
	kSrc = 0x8000'0000,
	kRef = 0x4000'0000,
	kDst = 0x2000'0000,
	kDstAfter = 0x1000'0000,
	kMask = 0xF000'0000
};

/// @brief Create a numeric value from a location.
/// @details Required to construct the values for the `Layout` enum.
/// @param location A `Location` enum.
/// @param value An integer to add.
/// @return An integer of the location enum value and the added @p value.
constexpr std::uint32_t operator+(const Location location, const std::uint32_t value) noexcept {
	return static_cast<std::uint32_t>(location) + value;
}

constexpr Location operator|(const Location lhs, const Location rhs) noexcept {
	return static_cast<Location>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr Location operator"" _loc(const char* const str, const std::size_t len) {
	if (len != 4) {
		throw std::invalid_argument("must be 4 characters");
	}
	assert(str[0] == 'S' || str[0] == '-');
	assert(str[1] == 'R' || str[1] == '-');
	assert(str[2] == 'D' || str[2] == '-');
	assert(str[3] == 'D' || str[3] == '-');
	return (str[0] == 'S' ? Location::kSrc : Location::kNone)
		   | (str[1] == 'R' ? Location::kRef : Location::kNone)
		   | (str[2] == 'D' ? Location::kDst : Location::kNone)
		   | (str[3] == 'D' ? Location::kDstAfter : Location::kNone);
}

enum class Capability : std::uint32_t {
	kSupportsChange = 0x0800'0000,
	kSupportsHardLink = 0x0400'0000,
	kSupportsFilenameCase = 0x0200'0000
};

/// @brief Create a numeric value from a Capability.
/// @details Required to construct the values for the `Layout` enum.
/// @param value An integer to add.
/// @param capability A `Capability` enum.
/// @return An integer of the location enum value and the added @p value.
constexpr std::uint32_t operator+(const std::uint32_t value, const Capability capability) noexcept {
	return static_cast<std::uint32_t>(capability) + value;
}

enum class Layout : std::uint32_t {
	kAdd = "S--D"_loc + 1,
	kAdded = "S-DD"_loc + 2 + Capability::kSupportsFilenameCase,
	kStaleAdded = "S-DD"_loc + 3 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kRetain = "SR-D"_loc + 4 + Capability::kSupportsFilenameCase,
	kRetained = "SRDD"_loc + 5 + Capability::kSupportsHardLink + Capability::kSupportsFilenameCase,
	kStaleRetained = "SRDD"_loc + 6 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kUpdate = "SR-D"_loc + 7 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kUpdated = "SRDD"_loc + 8 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kStaleUpdated = "SRDD"_loc + 9 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kTweaked = "SRDD"_loc + 10 + Capability::kSupportsChange + Capability::kSupportsHardLink + Capability::kSupportsFilenameCase,
	kRemove = "-RD-"_loc + 11 + Capability::kSupportsHardLink + Capability::kSupportsFilenameCase,
	kStaleRemove = "-RD-"_loc + 12 + Capability::kSupportsChange + Capability::kSupportsFilenameCase,
	kRemoved = "-R--"_loc + 13,
	kExtra = "--D-"_loc + 14
};

constexpr Layout kAllLayouts[] = {Layout::kAdd, Layout::kAdded, Layout::kStaleAdded, Layout::kRetain, Layout::kRetained, Layout::kStaleRetained, Layout::kUpdate, Layout::kUpdated, Layout::kStaleUpdated, Layout::kTweaked, Layout::kRemove, Layout::kStaleRemove, Layout::kRemoved, Layout::kExtra};

/// @brief Check if a `Location` is part of a `Layout`.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param location The `Location`.
/// @param layout The `Layout`.
/// @return `true` if the location is part of the layout, i.e. all bits of @p location are set in @p layout.
constexpr bool operator<(const Location location, const Layout layout) noexcept {
	return (static_cast<std::uint32_t>(location) & static_cast<std::uint32_t>(layout)) == static_cast<std::uint32_t>(location);
}

/// @brief Check if a `Layout` is part of another `Layout`.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param child The child `Layout`.
/// @param layout The parent `Layout`.
/// @return `true` if all `Location`s of @p child exist in @p parent.
constexpr bool operator<(const Layout child, const Layout parent) noexcept {
	return (static_cast<std::uint32_t>(child) & static_cast<std::uint32_t>(parent) & static_cast<std::uint32_t>(Location::kMask)) == (static_cast<std::uint32_t>(child) & static_cast<std::uint32_t>(Location::kMask));
}

/// @brief Check if a `Layout` supports a `Capability`.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param capability The `Capability`.
/// @param layout The `Layout`.
/// @return `true` if the layout supports the capability, i.e. all bits of @p capability are set in @p layout.
constexpr bool operator<(const Capability capability, const Layout layout) noexcept {
	return (static_cast<std::uint32_t>(layout) & static_cast<std::uint32_t>(capability)) == static_cast<std::uint32_t>(capability);
}

/// @brief Check if a `Mode` is supported by a `Layout`.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param mode The `Mode`.
/// @param layout The `Layout`.
/// @return `true` if the mode is supported by the @p layout.
constexpr bool operator<(const Mode mode, const Layout layout) noexcept {
	if (mode == Mode::kFileCopy || mode == Mode::kFolder) {
		return true;
	}
	return Capability::kSupportsHardLink < layout;
}

void PrintTo(const Layout layout, std::ostream* const os) {
	switch (layout) {
	case Layout::kAdd:
		*os << "Add";
		break;
	case Layout::kAdded:
		*os << "Added";
		break;
	case Layout::kStaleAdded:
		*os << "StaleAdded";
		break;
	case Layout::kRetain:
		*os << "Retain";
		break;
	case Layout::kRetained:
		*os << "Retained";
		break;
	case Layout::kStaleRetained:
		*os << "StaleRetained";
		break;
	case Layout::kUpdate:
		*os << "Update";
		break;
	case Layout::kUpdated:
		*os << "Updated";
		break;
	case Layout::kStaleUpdated:
		*os << "StaleUpdated";
		break;
	case Layout::kTweaked:
		*os << "Tweaked";
		break;
	case Layout::kRemove:
		*os << "Remove";
		break;
	case Layout::kStaleRemove:
		*os << "StaleRemove";
		break;
	case Layout::kRemoved:
		*os << "Removed";
		break;
	case Layout::kExtra:
		*os << "Extra";
		break;
	}
}

enum class Change : std::uint32_t {
	kNone = 0,
	kCreationTime = 0x01,
	kLastWriteTime = 0x02,
	kAttributeReadOnly = 0x04,
	kAttributeHidden = 0x08,
	kAttributeSystem = 0x10,
	kAttributeArchive = 0x20,
	kSize = 0x40,
	kContent = 0x80,
	kFilenameCase = 0x100
};

constexpr Change operator|(const Change lhs, const Change rhs) noexcept {
	return static_cast<Change>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

/// @brief Check if `Change` @p test is part of @p change.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param test The `Change` value to test if present.
/// @param change The `Change` value which is tested.
/// @return `true` if all flags of @p test are set in @p change.
constexpr bool operator<(const Change test, const Change change) noexcept {
	return (static_cast<std::uint32_t>(test) & static_cast<std::uint32_t>(change)) == static_cast<std::uint32_t>(test);
}

/// @brief Get the file attributes encoded in the `Change` enum.
/// @param change The `Change` value.
/// @return A valid value for windows file attributes.
constexpr DWORD operator*(const Change change) noexcept {
	return (Change::kAttributeReadOnly < change ? FILE_ATTRIBUTE_READONLY : 0)
		   | (Change::kAttributeHidden < change ? FILE_ATTRIBUTE_HIDDEN : 0)
		   | (Change::kAttributeSystem < change ? FILE_ATTRIBUTE_SYSTEM : 0)
		   | (Change::kAttributeArchive < change ? FILE_ATTRIBUTE_ARCHIVE : 0);
}

// assert that all attributes used in the tests are copied
static_assert((*static_cast<Change>(0xFFFF'FFFF) & BackupStrategy::kCopyAttributeMask) == *static_cast<Change>(0xFFFF'FFFF));

/// @brief Check if a `Layout` supports a `Change`.
/// @note The operator `<` is used like the subset operator in set theory. If is preferred over `&` because the latter is symmetric while `<` is not.
/// @param change The `Change`.
/// @param layout The `Layout`.
/// @return `true` if the layout supports the change, i.e. all capabilities required for the change are set in @p layout.
constexpr bool operator<(const Change change, const Layout layout) noexcept {
	if (change == Change::kNone) {
		return !(Capability::kSupportsChange < layout);
	}
	if (Change::kFilenameCase < change && !(Capability::kSupportsFilenameCase < layout)) {
		return false;
	}
	if (Change::kFilenameCase == change) {
		return true;
	}
	return Capability::kSupportsChange < layout;
}

const std::vector<Change> kAllFolderChanges{Change::kNone, Change::kCreationTime, Change::kLastWriteTime, Change::kAttributeReadOnly, Change::kAttributeHidden | Change::kAttributeSystem, Change::kAttributeArchive, Change::kFilenameCase, Change::kFilenameCase | Change::kAttributeArchive};
const std::vector<Change> kAllFileChanges{Change::kNone, Change::kCreationTime, Change::kLastWriteTime, Change::kAttributeReadOnly, Change::kAttributeHidden | Change::kAttributeSystem, Change::kAttributeArchive, Change::kSize, Change::kContent, Change::kFilenameCase, Change::kFilenameCase | Change::kAttributeReadOnly, Change::kFilenameCase | Change::kContent, Change::kFilenameCase | Change::kSize};

void PrintTo(const Change change, std::ostream* const os) {
	if (change == Change::kNone) {
		*os << "None";
		return;
	}
	if (Change::kFilenameCase < change) {
		*os << "Case";
	}
	if (Change::kCreationTime < change) {
		*os << "CreationTime";
	}
	if (Change::kLastWriteTime < change) {
		*os << "LastWriteTime";
	}
	if (*change) {
		*os << "Attribute";
		if (Change::kAttributeReadOnly < change) {
			*os << "R";
		}
		if (Change::kAttributeSystem < change) {
			*os << "S";
		}
		if (Change::kAttributeHidden < change) {
			*os << "H";
		}
		if (Change::kAttributeArchive < change) {
			*os << "A";
		}
	}
	if (Change::kSize < change) {
		*os << "Size";
	}
	if (Change::kContent < change) {
		*os << "Content";
	}
}

}  // namespace

class Backup_CustomRootTest : public Backup_Fixture {
protected:
	Backup_CustomRootTest()
		: Backup_Fixture(false) {
	}
};

class Backup_DataDrivenTest : public Backup_Fixture
	, public t::WithParamInterface<std::tuple<Mode, std::vector<std::pair<Layout, Change>>>> {
protected:
	Backup_DataDrivenTest(const bool enableRootScan = true)
		: Backup_Fixture(enableRootScan) {
		// empty
	}

	std::unique_ptr<FileBuilder> CreateFileBuilder(const Mode mode, const Layout layout, const Change change, const std::string& suffix = std::string()) {
		const std::string layoutName = t::PrintToString(layout) + suffix;
		std::wstring name;
		std::transform(layoutName.cbegin(), layoutName.cend(), std::back_inserter(name), [](const char ch) {
			return static_cast<wchar_t>(ch);
		});

		constexpr wchar_t kExtension[] = L".txt";

		FileBuilder file = mode == Mode::kFolder ? Folder((Change::kFilenameCase < change ? L"case" : L"") + name) : File((Change::kFilenameCase < change ? L"case" : L"") + name + kExtension);

		if (Location::kSrc < layout) {
			file.change().src();
			if (Change::kFilenameCase < change) {
				file.filename(L"Case" + name + (mode == Mode::kFolder ? L"" : kExtension));
			}
		}
		if (Location::kRef < layout) {
			file.change().ref();
			if (Change::kFilenameCase < change) {
				file.filename(L"cAse" + name + (mode == Mode::kFolder ? L"" : kExtension));
			}
		}
		if (Location::kDst < layout) {
			file.change().dst();
			if (Change::kFilenameCase < change) {
				file.filename(L"caSe" + name + (mode == Mode::kFolder ? L"" : kExtension));
			}
			if (mode == Mode::kFileHardLink) {
				if (!(Location::kRef < layout)) {
					THROW(std::exception(), "layout {} does not support hard links", file.srcPath(), t::PrintToString(layout));
				}
				file.ref().fileId(kMagicFileId);
			}
		}

		const bool creationTime = Change::kCreationTime < change;
		const bool lastWriteTime = Change::kLastWriteTime < change;
		const DWORD attributes = *change;
		const bool size = Change::kSize < change;
		const bool content = Change::kContent < change;

		if (mode == Mode::kFolder && (size || content)) {
			THROW(std::exception(), "folder {} does not support change {}", file.srcPath(), t::PrintToString(change));
		}

		if (!(Capability::kSupportsChange < layout) && (creationTime || lastWriteTime || attributes || size || content)) {
			THROW(std::exception(), "{} {} does not support changes", file.srcPath(), t::PrintToString(layout));
		}
		if (!(mode < layout)) {
			THROW(std::exception(), "{} {} does not support {}", file.srcPath(), t::PrintToString(layout), t::PrintToString(mode));
		}

		switch (layout) {
		case Layout::kAdd:
		case Layout::kAdded:
			// no change
			break;
		case Layout::kStaleAdded:
			if (creationTime) {
				file.change().src().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().src().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().src().attributes(attributes);
			}
			if (size) {
				file.change().src().size(kMagicFileSize);
			}
			if (content) {
				file.change().src().content(t::PrintToString(layout));
			}
			break;
		case Layout::kRetain:
		case Layout::kRetained:
			// no change
			break;
		case Layout::kStaleRetained:
			if (creationTime) {
				file.change().src().ref().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().src().ref().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().src().ref().attributes(attributes);
			}
			if (size) {
				file.change().src().ref().size(kMagicFileSize);
			}
			if (content) {
				file.change().src().ref().content(t::PrintToString(layout));
			}
			break;
		case Layout::kUpdate:
			if (creationTime) {
				file.change().src().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().src().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().src().attributes(attributes);
			}
			if (size) {
				file.change().src().size(kMagicFileSize);
			}
			if (content) {
				file.change().src().content(t::PrintToString(layout));
			}
			break;
		case Layout::kUpdated:
			if (creationTime) {
				file.change().src().dst().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().src().dst().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().src().dst().attributes(attributes);
			}
			if (size) {
				file.change().src().dst().size(kMagicFileSize);
			}
			if (content) {
				file.change().src().dst().content(t::PrintToString(layout));
			}
			break;
		case Layout::kStaleUpdated:
			if (creationTime) {
				file.change().src().creationTime(kMagicCreationTime).change().ref().creationTime(kMagicCreationTime + 1);
			}
			if (lastWriteTime) {
				file.change().src().lastWriteTime(kMagicLastWriteTime).change().ref().lastWriteTime(kMagicLastWriteTime + 1);
			}
			if (attributes) {
				file.change().src().attributes(attributes).change().ref().attributes(attributes | kMagicAttribute);
			}
			if (size) {
				file.change().src().size(kMagicFileSize).change().ref().size(kMagicFileSize + 1);
			}
			if (content) {
				file.change().src().content(t::PrintToString(layout)).change().ref().content(t::PrintToString(layout) + kMagicContentSuffix);
			}
			break;
		case Layout::kTweaked:
			if (creationTime) {
				file.change().src().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().src().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().src().attributes(attributes);
			}
			if (size) {
				file.change().src().size(kMagicFileSize);
			}
			if (content) {
				file.change().src().content(t::PrintToString(layout));
			}
			break;
		case Layout::kRemove:
			// no change
			break;
		case Layout::kStaleRemove:
			if (creationTime) {
				file.change().ref().creationTime(kMagicCreationTime);
			}
			if (lastWriteTime) {
				file.change().ref().lastWriteTime(kMagicLastWriteTime);
			}
			if (attributes) {
				file.change().ref().attributes(attributes);
			}
			if (size) {
				file.change().ref().size(kMagicFileSize);
			}
			if (content) {
				file.change().ref().content(t::PrintToString(layout));
			}
			break;
		case Layout::kRemoved:
		case Layout::kExtra:
			// no change
			break;
		}
		return std::make_unique<FileBuilder>(file);
	}

	void CheckBefore(const FileBuilder& file, const Mode mode, const Layout layout, const Change change) {
		const auto& files = Files();

		const auto src = files.find(file.srcPath());
		if (Location::kSrc < layout) {
			ASSERT_NE(src, files.cend());
			ASSERT_EQ(mode == Mode::kFolder, src->second.IsDirectory());

			if (Change::kFilenameCase < change) {
				ASSERT_THAT(src->second.filename, t::StartsWith(L"Case"));
			}
		} else {
			ASSERT_EQ(src, files.cend());
		}

		const auto ref = files.find(file.refPath());
		if (Location::kRef < layout) {
			ASSERT_NE(ref, files.cend());
			ASSERT_EQ(mode == Mode::kFolder, ref->second.IsDirectory());
			if (Change::kFilenameCase < change) {
				ASSERT_THAT(ref->second.filename, t::StartsWith(L"cAse"));
			}
		} else {
			ASSERT_EQ(ref, files.cend());
		}

		const auto dst = files.find(file.dstPath());
		if (Location::kDst < layout) {
			ASSERT_NE(dst, files.cend());
			ASSERT_EQ(mode == Mode::kFolder, dst->second.IsDirectory());
			if (Change::kFilenameCase < change) {
				ASSERT_THAT(dst->second.filename, t::StartsWith(L"caSe"));
			}
		} else {
			ASSERT_EQ(dst, files.cend());
		}

		if (mode == Mode::kFileHardLink) {
			ASSERT_EQ(kMagicFileId, ref->second.fileId);
			ASSERT_EQ(kMagicFileId, dst->second.fileId);
		} else if (mode == Mode::kFileCopy) {
			if (Location::kRef < layout) {
				ASSERT_NE(kMagicFileId, ref->second.fileId);
			}
			if (Location::kDst < layout) {
				ASSERT_NE(kMagicFileId, dst->second.fileId);
			}
			if (Location::kRef < layout && Location::kDst < layout) {
				ASSERT_NE(ref->second.fileId, dst->second.fileId);
			}
		}

		const bool creationTime = Change::kCreationTime < change;
		const bool lastWriteTime = Change::kLastWriteTime < change;
		const DWORD attributes = *change;
		const bool size = Change::kSize < change;
		const bool content = Change::kContent < change;

		switch (layout) {
		case Layout::kAdd:
		case Layout::kAdded:
			// do nothing
			break;
		case Layout::kStaleAdded:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_NE(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(dst->second.attributes, t::Not(HasAttributes(attributes)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_NE(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_NE(layoutName, dst->second.content);
			}
			break;
		case Layout::kRetain:
		case Layout::kRetained:
			// do nothing
			break;
		case Layout::kStaleRetained:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_EQ(kMagicCreationTime, ref->second.creationTime);
				ASSERT_NE(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_EQ(kMagicLastWriteTime, ref->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(ref->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(dst->second.attributes, t::Not(HasAttributes(attributes)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_EQ(kMagicFileSize, ref->second.size);
				ASSERT_NE(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_EQ(layoutName, ref->second.content);
				ASSERT_NE(layoutName, dst->second.content);
			}
			break;
		case Layout::kUpdate:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_NE(kMagicCreationTime, ref->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, ref->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(ref->second.attributes, t::Not(HasAttributes(attributes)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_NE(kMagicFileSize, ref->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_NE(layoutName, ref->second.content);
			}
			break;
		case Layout::kUpdated:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_NE(kMagicCreationTime, ref->second.creationTime);
				ASSERT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, ref->second.lastWriteTime);
				ASSERT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(ref->second.attributes, t::Not(HasAttributes(attributes)));
				ASSERT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_NE(kMagicFileSize, ref->second.size);
				ASSERT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_NE(layoutName, ref->second.content);
				ASSERT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kStaleUpdated:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_EQ(kMagicCreationTime + 1, ref->second.creationTime);
				ASSERT_GT(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_EQ(kMagicLastWriteTime + 1, ref->second.lastWriteTime);
				ASSERT_GT(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(ref->second.attributes, HasAttributes(attributes | kMagicAttribute));
				ASSERT_THAT(dst->second.attributes, t::Not(HasAttributes(attributes | kMagicAttribute)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_EQ(kMagicFileSize + 1, ref->second.size);
				ASSERT_GT(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_EQ(layoutName + kMagicContentSuffix, ref->second.content);
				ASSERT_THAT(dst->second.content.c_str(), t::Not(t::StartsWith(layoutName)));
			}
			break;
		case Layout::kTweaked:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, src->second.creationTime);
				ASSERT_NE(kMagicCreationTime, ref->second.creationTime);
				ASSERT_NE(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, src->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, ref->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(src->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(ref->second.attributes, t::Not(HasAttributes(attributes)));
				ASSERT_THAT(dst->second.attributes, t::Not(HasAttributes(attributes)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, src->second.size);
				ASSERT_NE(kMagicFileSize, ref->second.size);
				ASSERT_NE(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, src->second.content);
				ASSERT_NE(layoutName, ref->second.content);
				ASSERT_NE(layoutName, dst->second.content);
			}
			break;
		case Layout::kRemove:
			// do nothing
			break;
		case Layout::kStaleRemove:
			if (creationTime) {
				ASSERT_EQ(kMagicCreationTime, ref->second.creationTime);
				ASSERT_NE(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				ASSERT_EQ(kMagicLastWriteTime, ref->second.lastWriteTime);
				ASSERT_NE(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				ASSERT_THAT(ref->second.attributes, HasAttributes(attributes));
				ASSERT_THAT(dst->second.attributes, t::Not(HasAttributes(attributes)));
			}
			if (size) {
				ASSERT_EQ(kMagicFileSize, ref->second.size);
				ASSERT_NE(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				ASSERT_EQ(layoutName, ref->second.content);
				ASSERT_NE(layoutName, dst->second.content);
			}
			break;
		case Layout::kRemoved:
		case Layout::kExtra:
			// do nothing
			break;
		}
	}

	void CheckAfter(const FileBuilder& file, const Mode mode, const Layout layout, const Change change) {
		const auto& files = Files();

		const auto src = files.find(file.srcPath());
		if (Location::kSrc < layout) {
			ASSERT_NE(src, files.cend());
		} else {
			EXPECT_EQ(src, files.cend());
		}

		const auto ref = files.find(file.refPath());
		if (Location::kRef < layout) {
			ASSERT_NE(ref, files.cend());
		} else {
			EXPECT_EQ(ref, files.cend());
		}

		const auto dst = files.find(file.dstPath());
		if (Location::kDstAfter < layout) {
			ASSERT_NE(dst, files.cend());
			if (Change::kFilenameCase < change) {
				EXPECT_THAT(dst->second.filename, t::StartsWith(L"Case"));
			}
		} else {
			EXPECT_EQ(dst, files.cend());
		}

		const bool creationTime = Change::kCreationTime < change;
		const bool lastWriteTime = Change::kLastWriteTime < change;
		const DWORD attributes = *change;
		const bool size = Change::kSize < change;
		const bool content = Change::kContent < change;

		switch (layout) {
		case Layout::kAdd:
		case Layout::kAdded:
			// do nothing
			break;
		case Layout::kStaleAdded:
			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kRetain:
			if (mode != Mode::kFolder) {
				EXPECT_EQ(ref->second.fileId, dst->second.fileId);
			}
			break;
		case Layout::kRetained:
			if (mode == Mode::kFileHardLink) {
				EXPECT_EQ(kMagicFileId, dst->second.fileId);
			} else if (mode == Mode::kFileCopy) {
				EXPECT_NE(ref->second.fileId, dst->second.fileId);
			}
			break;
		case Layout::kStaleRetained:
			if (mode != Mode::kFolder) {
				if (change == Change::kFilenameCase) {
					EXPECT_NE(ref->second.fileId, dst->second.fileId);
				} else {
					EXPECT_EQ(ref->second.fileId, dst->second.fileId);
				}
			}

			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kUpdate:
			if (mode != Mode::kFolder) {
				if (change == Change::kFilenameCase) {
					EXPECT_EQ(ref->second.fileId, dst->second.fileId);
				} else {
					EXPECT_NE(ref->second.fileId, dst->second.fileId);
				}
			}

			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kUpdated:
			if (mode != Mode::kFolder) {
				EXPECT_NE(ref->second.fileId, dst->second.fileId);
			}

			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kStaleUpdated:
			if (mode != Mode::kFolder) {
				EXPECT_NE(ref->second.fileId, dst->second.fileId);
			}

			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kTweaked:
			if (mode == Mode::kFileHardLink) {
				if (change == Change::kFilenameCase) {
					EXPECT_EQ(ref->second.fileId, dst->second.fileId);
				} else {
					EXPECT_NE(kMagicFileId, dst->second.fileId);
					EXPECT_NE(ref->second.fileId, dst->second.fileId);
				}
			} else if (mode != Mode::kFolder) {
				EXPECT_NE(ref->second.fileId, dst->second.fileId);
			}

			if (creationTime) {
				EXPECT_EQ(kMagicCreationTime, dst->second.creationTime);
			}
			if (lastWriteTime) {
				EXPECT_EQ(kMagicLastWriteTime, dst->second.lastWriteTime);
			}
			if (attributes) {
				EXPECT_THAT(dst->second.attributes, HasAttributes(attributes));
			}
			if (size) {
				EXPECT_EQ(kMagicFileSize, dst->second.size);
			}
			if (content) {
				const std::string layoutName = t::PrintToString(layout);
				EXPECT_EQ(layoutName, dst->second.content);
			}
			break;
		case Layout::kRemove:
		case Layout::kStaleRemove:
		case Layout::kRemoved:
		case Layout::kExtra:
			// do nothing
			break;
		}
	}
};

class Backup_CustomDataDrivenTest : public Backup_DataDrivenTest {
private:
	class FakeBackupStrategy : public BackupStrategy {
	public:
		FakeBackupStrategy(BackupFileSystem_Fake& fileSystem)
			: m_fileSystem(fileSystem) {
			// empty
		}

	public:
		// Inherited via BackupStrategy
		virtual bool Exists(const Path& path) const override {
			return m_fileSystem.Exists(path);
		}
		virtual bool IsDirectory(const Path& path) const override {
			return m_fileSystem.IsDirectory(path);
		}
		virtual bool Compare(const Path& src, const Path& target, FileComparer&) const override {
			return m_fileSystem.Compare(src, target);
		}
		virtual void CreateDirectoryRecursive(const Path& path) const override {
			m_fileSystem.CreateDirectoryRecursive(path);
		}
		virtual void SetAttributes(const Path& path, const ScannedFile& attributesSource) const override {
			m_fileSystem.SetAttributes(path, attributesSource);
		}
		virtual void SetSecurity(const Path& path, const ScannedFile& securitySource) const override {
			m_fileSystem.SetSecurity(path, securitySource);
		}
		virtual void Rename(const Path& existingName, const Path& newName) const override {
			m_fileSystem.Rename(existingName, newName);
		}
		virtual void Copy(const Path& source, const Path& target) const override {
			m_fileSystem.Copy(source, target);
		}
		virtual void Delete(const Path& path) const override {
			m_fileSystem.Delete(path);
		}
		virtual void Scan(const Path& path, DirectoryScanner&, std::vector<ScannedFile>& directories, std::vector<ScannedFile>& files, const DirectoryScanner::Flags flags, const ScannerFilter& filter) const override {
			m_fileSystem.Scan(path, directories, files, flags, filter);
		}
		virtual void WaitForScan(DirectoryScanner&) const override {
			// empty
		}
		virtual void CreateDirectory(const Path& path, const Path& templatePath, const ScannedFile& securitySource) const override {
			m_fileSystem.CreateDirectory(path, templatePath, securitySource);
		}
		virtual void CreateHardLink(const Path& path, const Path& existing) const override {
			m_fileSystem.CreateHardLink(path, existing);
		}

	private:
		BackupFileSystem_Fake& m_fileSystem;
	};

protected:
	Backup_CustomDataDrivenTest()
		: Backup_DataDrivenTest(false) {
		// empty
	}

	void AddToBackupSet(std::vector<std::tuple<std::unique_ptr<FileBuilder>, Mode, Layout, Change>>& files, const std::size_t level, const std::size_t maxLevel, const std::size_t parentIndex) {
		std::uint32_t id = 0;

		for (std::size_t layoutIndex = 0; layoutIndex < sizeof(kAllLayouts) / sizeof(kAllLayouts[0]); ++layoutIndex) {
			const Layout layout = kAllLayouts[layoutIndex];
			if (level == 0) {
				if (!(Location::kSrc < layout)) {
					// does not make sense to have no src folder at root level
					continue;
				}
			} else {
				if (!(layout < std::get<2>(files.at(parentIndex)))) {
					// some folders for layout do not exist in parent
					continue;
				}
			}
			FileBuilder& parent = parentIndex ? *std::get<0>(files.at(parentIndex)) : m_root;
			if (Mode::kFolder < layout) {
				for (const Change change : kAllFolderChanges) {
					if (change < layout) {
						{
							std::unique_ptr<FileBuilder> pFile = CreateFileBuilder(Mode::kFolder, layout, change, fmt::format("-{}", id++));
							pFile->disableExpect();
							if (level == 0) {
								pFile->change().src().enablePathFunctions();
							}
							parent.children(*pFile);
							files.push_back(std::make_tuple(std::move(pFile), Mode::kFolder, layout, change));
						}
						{
							std::unique_ptr<FileBuilder> pFile = CreateFileBuilder(Mode::kFolder, layout, change, fmt::format("-{}", id++));
							pFile->disableExpect();
							if (level == 0) {
								pFile->change().src().enablePathFunctions();
							}
							parent.children(*pFile);
							files.push_back(std::make_tuple(std::move(pFile), Mode::kFolder, layout, change));
							if (level < maxLevel) {
								AddToBackupSet(files, level + 1, maxLevel, files.size() - 1);
							}
						}
					}
				}
			}
			if (level > 0 && Mode::kFileCopy < layout) {
				for (const Change change : kAllFileChanges) {
					if (change < layout) {
						std::unique_ptr<FileBuilder> pFile = CreateFileBuilder(Mode::kFileCopy, layout, change, fmt::format("-{}", id++));
						pFile->disableExpect();
						parent.children(*pFile);
						files.push_back(std::make_tuple(std::move(pFile), Mode::kFileCopy, layout, change));
					}
				}
			}
			if (level > 0 && Mode::kFileHardLink < layout) {
				for (const Change change : kAllFileChanges) {
					if (change < layout) {
						std::unique_ptr<FileBuilder> pFile = CreateFileBuilder(Mode::kFileHardLink, layout, change, fmt::format("-{}", id++));
						pFile->disableExpect();
						parent.children(*pFile);
						files.push_back(std::make_tuple(std::move(pFile), Mode::kFileHardLink, layout, change));
					}
				}
			}
		}
	}

	Backup::Statistics VerifyBackup(const std::vector<Path>& backupFolders) override {
		return RunVerified(backupFolders, [this](const auto& backupFolders) {
			FakeBackupStrategy strategy(m_fileSystem);
			Backup backup(strategy);
			return backup.CreateBackup(backupFolders, m_ref, m_dst);
		});
	}
};

using Backup_Test = Backup_Fixture;

TEST_P(Backup_DataDrivenTest, CreateBackup_Call_Return) {
	const Mode mode = std::get<0>(GetParam());
	const std::vector<std::pair<Layout, Change>>& tree = std::get<1>(GetParam());

	std::uint64_t foldersTotal = 0;
	std::uint64_t filesTotal = 0;

	std::uint64_t addedFolders = 0;
	std::uint64_t addedFiles = 0;
	std::uint64_t updatedFolders = 0;
	std::uint64_t updatedFiles = 0;
	std::uint64_t retainedFolders = 0;
	std::uint64_t retainedFiles = 0;
	std::uint64_t removedFolders = 0;
	std::uint64_t removedFiles = 0;

	std::uint64_t replacedFiles = 0;

	std::uint64_t bytesTotal = 0;
	std::uint64_t bytesInHardLinks = 0;
	std::uint64_t bytesCopied = 0;
	std::uint64_t bytesCreatedInHardLinks = 0;

	std::uint64_t addedBytes = 0;
	std::uint64_t updatedBytes = 0;
	std::uint64_t retainedBytes = 0;
	std::uint64_t removedBytes = 0;

	std::uint64_t replacedBytes = 0;

	std::vector<std::unique_ptr<FileBuilder>> files;
	files.reserve(tree.size());

	auto modeForLevel = [mode, &tree](const std::size_t level) {
		return level == 0 || level + 1 < tree.size() ? Mode::kFolder : mode;
	};

	for (std::size_t level = 0; level < tree.size(); ++level) {
		const std::pair<Layout, Change>& spec = tree.at(level);
		const Layout layout = spec.first;
		const Change change = spec.second;
		const Mode fileMode = modeForLevel(level);
		std::unique_ptr<FileBuilder> pFile = CreateFileBuilder(fileMode, layout, change);
		if (level == 0) {
			pFile->change().src().enablePathFunctions();
			m_root.children(*pFile);
		} else {
			files.back()->children(*pFile);
		}

		// yes, that's ugly, but easier to understand than calculating the changes
		switch (layout) {
		case Layout::kAdd:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			addedFolders += Mode::kFolder == fileMode ? 1 : 0;
			addedFiles += Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesCopied += pFile->srcEntry().size;
			addedBytes += pFile->srcEntry().size;
			break;
		case Layout::kAdded:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			retainedFolders += change == Change::kNone ? Mode::kFolder == fileMode ? 1 : 0 : 0;
			retainedFiles += change == Change::kNone ? Mode::kFolder == fileMode ? 0 : 1 : 0;

			updatedFolders += change == Change::kNone ? 0 : Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += change == Change::kNone ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			retainedBytes += change == Change::kNone ? pFile->srcEntry().size : 0;
			updatedBytes += change == Change::kNone ? 0 : pFile->srcEntry().size;
			break;
		case Layout::kStaleAdded:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			updatedFolders += Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += Mode::kFolder == fileMode ? 0 : 1;

			replacedFiles += change == Change::kFilenameCase ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesCopied += change == Change::kFilenameCase ? 0 : pFile->srcEntry().size;
			updatedBytes += pFile->srcEntry().size;
			replacedBytes += change == Change::kFilenameCase ? 0 : pFile->dstEntry().size;
			break;
		case Layout::kRetain:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			addedFolders += Mode::kFolder == fileMode ? 1 : 0;
			addedFiles += Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesInHardLinks += pFile->srcEntry().size;
			bytesCreatedInHardLinks += pFile->srcEntry().size;
			addedBytes += pFile->srcEntry().size;
			break;
		case Layout::kRetained:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			retainedFolders += Change::kNone == change ? Mode::kFolder == fileMode ? 1 : 0 : 0;
			retainedFiles += Change::kNone == change ? Mode::kFolder == fileMode ? 0 : 1 : 0;

			updatedFolders += Change::kNone == change ? 0 : Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += Change::kNone == change ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesInHardLinks += Mode::kFileHardLink == fileMode ? pFile->srcEntry().size : 0;
			retainedBytes += Change::kNone == change ? pFile->srcEntry().size : 0;
			updatedBytes += Change::kNone == change ? 0 : pFile->srcEntry().size;
			break;
		case Layout::kStaleRetained:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			updatedFolders += Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += Mode::kFolder == fileMode ? 0 : 1;

			replacedFiles += Change::kFilenameCase == change ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesInHardLinks += Change::kFilenameCase == change && Mode::kFileCopy == fileMode ? 0 : pFile->srcEntry().size;
			bytesCreatedInHardLinks += Change::kFilenameCase == change ? 0 : pFile->srcEntry().size;
			updatedBytes += pFile->srcEntry().size;
			replacedBytes += Change::kFilenameCase == change ? 0 : pFile->dstEntry().size;
			break;
		case Layout::kUpdate:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			addedFolders += Mode::kFolder == fileMode ? 1 : 0;
			addedFiles += Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesInHardLinks += Change::kFilenameCase == change ? pFile->srcEntry().size : 0;
			bytesCopied += Change::kFilenameCase == change ? 0 : pFile->srcEntry().size;
			bytesCreatedInHardLinks += Change::kFilenameCase == change ? pFile->srcEntry().size : 0;
			addedBytes += pFile->srcEntry().size;
			break;
		case Layout::kUpdated:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			// other changes than kFilenameCase only refer to ref
			retainedFolders += Change::kFilenameCase < change ? 0 : Mode::kFolder == fileMode ? 1 : 0;
			retainedFiles += Change::kFilenameCase < change ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			updatedFolders += Change::kFilenameCase < change ? Mode::kFolder == fileMode ? 1 : 0 : 0;
			updatedFiles += Change::kFilenameCase < change ? Mode::kFolder == fileMode ? 0 : 1 : 0;

			bytesTotal += pFile->srcEntry().size;
			retainedBytes += Change::kFilenameCase < change ? 0 : pFile->srcEntry().size;
			updatedBytes += Change::kFilenameCase < change ? pFile->srcEntry().size : 0;
			break;
		case Layout::kStaleUpdated:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			updatedFolders += Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += Mode::kFolder == fileMode ? 0 : 1;

			replacedFiles += Change::kFilenameCase == change ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesCopied += Change::kFilenameCase == change ? 0 : pFile->srcEntry().size;
			updatedBytes += pFile->srcEntry().size;
			replacedBytes += Change::kFilenameCase == change ? 0 : pFile->dstEntry().size;
			break;
		case Layout::kTweaked:
			foldersTotal += Mode::kFolder == fileMode ? 1 : 0;
			filesTotal += Mode::kFolder == fileMode ? 0 : 1;

			updatedFolders += Mode::kFolder == fileMode ? 1 : 0;
			updatedFiles += Mode::kFolder == fileMode ? 0 : 1;

			replacedFiles += Change::kFilenameCase == change ? 0 : Mode::kFolder == fileMode ? 0 : 1;

			bytesTotal += pFile->srcEntry().size;
			bytesInHardLinks += Change::kFilenameCase == change && Mode::kFileHardLink == fileMode ? pFile->srcEntry().size : 0;
			bytesCopied += Change::kFilenameCase == change ? 0 : pFile->srcEntry().size;
			updatedBytes += pFile->srcEntry().size;
			replacedBytes += Change::kFilenameCase == change ? 0 : pFile->dstEntry().size;
			break;
		case Layout::kRemove:
			removedFolders += Mode::kFolder == fileMode ? 1 : 0;
			removedFiles += Mode::kFolder == fileMode ? 0 : 1;
			removedBytes += pFile->dstEntry().size;
			break;
		case Layout::kStaleRemove:
			removedFolders += Mode::kFolder == fileMode ? 1 : 0;
			removedFiles += Mode::kFolder == fileMode ? 0 : 1;
			removedBytes += pFile->dstEntry().size;
			break;
		case Layout::kRemoved:
			// nothing
			break;
		case Layout::kExtra:
			removedFolders += Mode::kFolder == fileMode ? 1 : 0;
			removedFiles += Mode::kFolder == fileMode ? 0 : 1;
			removedBytes += pFile->dstEntry().size;
			break;
		}

		files.push_back(std::move(pFile));
	}

	for (std::size_t level = 0; level < files.size(); ++level) {
		const std::pair<Layout, Change>& spec = tree.at(level);
		const Layout layout = spec.first;
		const Change change = spec.second;
		CheckBefore(*files.at(level), modeForLevel(level), layout, change);
	}

	const Backup::Statistics statistics = VerifyBackup({files.front()->srcPath()});

	for (std::size_t level = 0; level < files.size(); ++level) {
		const std::pair<Layout, Change>& spec = tree.at(level);
		const Layout layout = spec.first;
		const Change change = spec.second;
		CheckAfter(*files.at(level), modeForLevel(level), layout, change);
	}

	EXPECT_EQ(foldersTotal, statistics.GetFolders());
	EXPECT_EQ(filesTotal, statistics.GetFiles());

	EXPECT_EQ(addedFolders, statistics.GetAdded().GetFolders());
	EXPECT_EQ(addedFiles, statistics.GetAdded().GetFiles());
	EXPECT_EQ(addedBytes, statistics.GetAdded().GetSize());

	EXPECT_EQ(updatedFolders, statistics.GetUpdated().GetFolders());
	EXPECT_EQ(updatedFiles, statistics.GetUpdated().GetFiles());
	EXPECT_EQ(updatedBytes, statistics.GetUpdated().GetSize());

	EXPECT_EQ(retainedFolders, statistics.GetRetained().GetFolders());
	EXPECT_EQ(retainedFiles, statistics.GetRetained().GetFiles());
	EXPECT_EQ(retainedBytes, statistics.GetRetained().GetSize());

	EXPECT_EQ(removedFolders, statistics.GetRemoved().GetFolders());
	EXPECT_EQ(removedFiles, statistics.GetRemoved().GetFiles());
	EXPECT_EQ(removedBytes, statistics.GetRemoved().GetSize());

	EXPECT_EQ(0, statistics.GetReplaced().GetFolders());
	EXPECT_EQ(replacedFiles, statistics.GetReplaced().GetFiles());
	EXPECT_EQ(replacedBytes, statistics.GetReplaced().GetSize());

	EXPECT_EQ(bytesTotal, statistics.GetBytesTotal());
	EXPECT_EQ(bytesInHardLinks, statistics.GetBytesInHardLinks());

	EXPECT_EQ(bytesCopied, statistics.GetBytesCopied());
	EXPECT_EQ(bytesCreatedInHardLinks, statistics.GetBytesCreatedInHardLinks());
}

//
// Special cases for root folders only
//
TEST_F(Backup_Test, CreateBackup_RootUnprocessed_DoNothing) {
	auto copied = Folder(L"Retained").src().enablePathFunctions().ref().dst();
	auto folder = Folder(L"Unprocessed").disableExpect().src();
	m_root.children(copied, folder);

	ASSERT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.dstPath()))));

	VerifyBackup({copied.srcPath()});

	EXPECT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(folder.dstPath()))));
}

TEST_F(Backup_Test, CreateBackup_RootIgnored_KeepFolder) {
	auto copied = Folder(L"Copied").src().enablePathFunctions().ref().dst();
	auto folder = Folder(L"Ignored").disableExpect().dst();
	m_root.children(copied, folder);

	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.srcPath()))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	ASSERT_THAT(Files(), t::Contains(t::Key(folder.dstPath())));

	VerifyBackup({copied.srcPath()});

	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(folder.srcPath()))));
	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	EXPECT_THAT(Files(), t::Contains(t::Key(folder.dstPath())));
}

//
// Missing root target folder
//
TEST_F(Backup_CustomRootTest, CreateBackup_RootAddAndDstMissing_CreateRootFolder) {
	m_root.change().ref().dst().remove();
	EXPECT_CALL(m_strategy, Scan(m_src, t::_, t::_, t::_, t::_, t::_));
	EXPECT_CALL(m_strategy, CreateDirectoryRecursive(m_dst));

	auto folder = Folder(L"Add").src().enablePathFunctions();
	m_root.children(folder);

	ASSERT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(m_ref))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(m_dst))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.dstPath()))));

	VerifyBackup({folder.srcPath()});

	EXPECT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(m_ref))));
	EXPECT_THAT(Files(), t::Not(t::Contains(t::Key(folder.refPath()))));
	EXPECT_THAT(Files(), t::Contains(t::Key(m_dst)));
	EXPECT_THAT(Files(), t::Contains(t::Key(folder.dstPath())));
}

TEST_F(Backup_CustomRootTest, CreateBackup_RootRetainAndDstMissing_CreateRootFolder) {
	m_root.change().dst().remove();
	EXPECT_CALL(m_strategy, Scan(m_src, t::_, t::_, t::_, t::_, t::_));
	EXPECT_CALL(m_strategy, Scan(m_ref, t::_, t::_, t::_, t::_, t::_));
	EXPECT_CALL(m_strategy, CreateDirectoryRecursive(m_dst));

	auto folder = Folder(L"Retain").src().enablePathFunctions().ref();
	m_root.children(folder);

	ASSERT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	ASSERT_THAT(Files(), t::Contains(t::Key(folder.refPath())));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(m_dst))));
	ASSERT_THAT(Files(), t::Not(t::Contains(t::Key(folder.dstPath()))));

	VerifyBackup({folder.srcPath()});

	EXPECT_THAT(Files(), t::Contains(t::Key(folder.srcPath())));
	EXPECT_THAT(Files(), t::Contains(t::Key(folder.refPath())));
	EXPECT_THAT(Files(), t::Contains(t::Key(m_dst)));
	EXPECT_THAT(Files(), t::Contains(t::Key(folder.dstPath())));
}

//
// Complex Setting
//

TEST_F(Backup_CustomDataDrivenTest, CreateBackup_Large_Return) {
	std::vector<std::tuple<std::unique_ptr<FileBuilder>, Mode, Layout, Change>> files;
	files.reserve(16384);

	AddToBackupSet(files, 0, 1, 0);

	std::vector<Path> paths;
	for (const auto& file : Files()) {
		if (file.second.IsDirectory() && file.first == m_src / file.second.filename && file.first != m_src) {
			paths.push_back(file.first);
		}
	}

	for (const auto& file : files) {
		const std::unique_ptr<FileBuilder>& pFile = std::get<0>(file);
		const Mode mode = std::get<1>(file);
		const Layout layout = std::get<2>(file);
		const Change change = std::get<3>(file);
		CheckBefore(*pFile, mode, layout, change);
	}

	VerifyBackup(paths);

	for (const auto& file : files) {
		const std::unique_ptr<FileBuilder>& pFile = std::get<0>(file);
		const Mode mode = std::get<1>(file);
		const Layout layout = std::get<2>(file);
		const Change change = std::get<3>(file);
		CheckAfter(*pFile, mode, layout, change);
	}
}

TEST(Backup_RealTest, DISABLED_CreateBackup_Check_Return) {
	std::vector src{Path(L"T:\\Test")};

	//DryRunBackupStrategy strategy;
	WritingBackupStrategy strategy;
	Backup backup(strategy);
	backup.CreateBackup(src, Path(L"V:\\ref"), Path(L"V:\\out"));
}

namespace {

void AddChildren(const Mode mode, std::vector<std::tuple<Mode, std::vector<std::pair<Layout, Change>>>>& result, const std::vector<std::pair<Layout, Change>>& parent, const std::uint32_t level, const std::uint32_t maxLevel, const std::vector<Change>& nodeChanges, const std::vector<Change>& leafChanges) {
	if (level > maxLevel) {
		result.push_back(std::make_tuple(mode, parent));
		return;
	}
	for (std::size_t layoutIndex = 0; layoutIndex < sizeof(kAllLayouts) / sizeof(kAllLayouts[0]); ++layoutIndex) {
		const Layout layout = kAllLayouts[layoutIndex];
		if (!(mode < layout)) {
			// mode is not supported by layout
			continue;
		}
		if (level == 0) {
			if (!(Location::kSrc < layout)) {
				// does not make sense to have no src folder at root level
				continue;
			}
		} else {
			const Layout parentLayout = parent.back().first;
			if (!(layout < parentLayout)) {
				// some folders for layout do not exist in parent
				continue;
			}
		}

		const std::vector<Change>& changes = level < maxLevel ? nodeChanges : leafChanges;
		for (const Change change : changes) {
			if (change < layout) {
				std::vector<std::pair<Layout, Change>> tree = parent;
				tree.emplace_back(std::make_pair(layout, change));
				AddChildren(mode, result, tree, level + 1, maxLevel, nodeChanges, leafChanges);
			}
		}
	}
}

std::vector<std::tuple<Mode, std::vector<std::pair<Layout, Change>>>> GetParameters(const Mode mode, const std::uint32_t maxLevel, const std::vector<Change>& nodeChanges, const std::vector<Change>& leafChanges) {
	std::vector<std::tuple<Mode, std::vector<std::pair<Layout, Change>>>> result;
	AddChildren(mode, result, {}, 0, maxLevel, nodeChanges, leafChanges);
	return result;
}

std::string GetNameForModeAndTree(const std::tuple<Mode, std::vector<std::pair<Layout, Change>>>& param) {
	const Mode mode = std::get<0>(param);
	const std::vector<std::pair<Layout, Change>>& tree = std::get<1>(param);

	std::string name = t::PrintToString(mode);
	for (const auto& element : tree) {
		name += "_" + t::PrintToString(element.first) + (element.second == Change::kNone ? "" : t::PrintToString(element.second));
	}
	return name;
}

std::string GetBackupTestName(const t::TestParamInfo<Backup_DataDrivenTest::ParamType>& param) {
	// MUST start with a number, else google test adapter cannot find the source code
	return fmt::format("{:03}_{}", param.index, GetNameForModeAndTree(param.param));
}

}  // namespace

TEST(Backup_SetupTest, GetParameters_Call_ReturnValues) {
	std::vector<std::tuple<Mode, std::vector<std::pair<Layout, Change>>>> parameters = GetParameters(Mode::kFolder, 1, {Change::kNone}, kAllFolderChanges);
	// check that test names are actually generated
	EXPECT_THAT(parameters, t::SizeIs(83));
	// check some names
	EXPECT_THAT(parameters, t::Contains(t::ResultOf(GetNameForModeAndTree, "Folder_Add_Add")));
	EXPECT_THAT(parameters, t::Contains(t::ResultOf(GetNameForModeAndTree, "Folder_Retain_Removed")));
}

INSTANTIATE_TEST_SUITE_P(Backup_RootTest, Backup_DataDrivenTest, t::ValuesIn(GetParameters(Mode::kFolder, 0, {}, kAllFolderChanges)), GetBackupTestName);
INSTANTIATE_TEST_SUITE_P(Backup_FolderTest, Backup_DataDrivenTest, t::ValuesIn(GetParameters(Mode::kFolder, 1, {Change::kNone}, kAllFolderChanges)), GetBackupTestName);
INSTANTIATE_TEST_SUITE_P(Backup_FileTest, Backup_DataDrivenTest, t::ValuesIn(GetParameters(Mode::kFileCopy, 1, {Change::kNone}, kAllFileChanges)), GetBackupTestName);
INSTANTIATE_TEST_SUITE_P(Backup_HardLinkTest, Backup_DataDrivenTest, t::ValuesIn(GetParameters(Mode::kFileHardLink, 1, {Change::kNone}, kAllFileChanges)), GetBackupTestName);

}  // namespace systools::test
