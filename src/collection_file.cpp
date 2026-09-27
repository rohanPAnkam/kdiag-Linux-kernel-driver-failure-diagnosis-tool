// Reads collection JSON and reports consistent file, parse, and schema errors.
#include "kdiag/collection_file.hpp"

#include <fstream>
#include <stdexcept>

namespace kdiag {

CollectionRun LoadCollectionRun(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (const std::exception& error) {
        throw std::runtime_error("failed to parse " + path + ": " + error.what());
    }

    try {
        return CollectionRun::FromJson(json);
    } catch (const std::exception& error) {
        throw std::runtime_error(path +
            " doesn't match the expected events.json schema: " + error.what());
    }
}

}  // namespace kdiag