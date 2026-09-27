// Verifies collection file loading and consistent diagnostics for invalid input.
#include "kdiag/collection_file.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace kdiag {
namespace {

std::filesystem::path TestFilePath(const std::string& suffix) {
    return std::filesystem::temp_directory_path() /
        ("kdiag_collection_file_" + suffix + ".json");
}

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path);
    output << contents;
}

TEST(CollectionFileTest, LoadsCollectionRunJson) {
    CollectionRun expected;
    expected.driver_name = "driver_name";
    Event event;
    event.timestamp_ns = 42;
    event.message = "test event";
    expected.events.push_back(event);
    const auto path = TestFilePath("valid");
    WriteFile(path, expected.ToJson().dump());

    const CollectionRun actual = LoadCollectionRun(path.string());

    EXPECT_EQ(actual.driver_name, expected.driver_name);
    ASSERT_EQ(actual.events.size(), 1u);
    EXPECT_EQ(actual.events[0].message, "test event");
    std::filesystem::remove(path);
}

TEST(CollectionFileTest, ReportsMissingFilePath) {
    const auto path = TestFilePath("missing");
    std::filesystem::remove(path);

    try {
        (void)LoadCollectionRun(path.string());
        FAIL() << "expected a file-open error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find(path.string()), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("cannot open"), std::string::npos);
    }
}

TEST(CollectionFileTest, ReportsInvalidJson) {
    const auto path = TestFilePath("invalid_json");
    WriteFile(path, "{");

    EXPECT_THROW(LoadCollectionRun(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(CollectionFileTest, ReportsSchemaErrors) {
    const auto path = TestFilePath("invalid_schema");
    WriteFile(path, "{}");

    try {
        (void)LoadCollectionRun(path.string());
        FAIL() << "expected a schema error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("schema"), std::string::npos);
    }
    std::filesystem::remove(path);
}

}  // namespace
}  // namespace kdiag