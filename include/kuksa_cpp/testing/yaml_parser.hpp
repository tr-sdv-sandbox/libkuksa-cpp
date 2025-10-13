#pragma once

#include "test_models.hpp"
#include <string>
#include <yaml-cpp/yaml.h>

namespace sdv {
namespace testing {

class YamlParser {
public:
    static TestSuite parse_file(const std::string& file_path);

private:
    static TestValue parse_value(const YAML::Node& node);
    static TestStep parse_step(const YAML::Node& node);
    static TestCase parse_test_case(const YAML::Node& node);
    static Fixture parse_fixture(const YAML::Node& node);
};

} // namespace testing
} // namespace kuksa
