#include "kuksa_cpp/testing/yaml_parser.hpp"
#include <yaml-cpp/yaml.h>
#include <glog/logging.h>
#include <stdexcept>

namespace sdv {
namespace testing {

TestValue YamlParser::parse_value(const YAML::Node& node) {
    if (node.IsScalar()) {
        // Try bool first
        try {
            return node.as<bool>();
        } catch (...) {}

        // Try int
        try {
            return node.as<int32_t>();
        } catch (...) {}

        // Try float
        try {
            return node.as<float>();
        } catch (...) {}

        // Fall back to string
        try {
            return node.as<std::string>();
        } catch (...) {}
    }

    throw std::runtime_error("Unsupported value type");
}

TestStep YamlParser::parse_step(const YAML::Node& node) {
    TestStep step;

    if (node["inject"]) {
        step.type = StepType::INJECT;
        auto inject_node = node["inject"];

        InjectData data;
        data.path = inject_node["path"].as<std::string>();
        data.value = parse_value(inject_node["value"]);
        // Note: RPC type (Actuate vs PublishValue) is determined automatically
        // by the wrapper based on signal type from KUKSA metadata

        step.data = data;
    }
    else if (node["expect"]) {
        step.type = StepType::EXPECT;
        auto expect_node = node["expect"];

        ExpectData data;
        data.path = expect_node["path"].as<std::string>();
        data.value = parse_value(expect_node["value"]);
        // Note: Signal type determines whether to read sensor or actuator ACTUAL

        step.data = data;

        // Parse timeout if present in expect block
        if (expect_node["timeout"]) {
            step.timeout = expect_node["timeout"].as<double>();
        }
    }
    else if (node["wait"]) {
        step.type = StepType::WAIT;
        WaitData data;

        // Try to get as number first, then as string
        try {
            data.seconds = node["wait"].as<double>();
        } catch (...) {
            std::string wait_str = node["wait"].as<std::string>();
            // Parse "1.5s" format
            if (wait_str.back() == 's') {
                data.seconds = std::stod(wait_str.substr(0, wait_str.size() - 1));
            } else {
                data.seconds = std::stod(wait_str);
            }
        }

        step.data = data;
    }
    else if (node["log"]) {
        step.type = StepType::LOG;
        LogData data;
        data.message = node["log"].as<std::string>();
        step.data = data;
    }
    else if (node["expect_state"]) {
        step.type = StepType::EXPECT_STATE;
        auto state_node = node["expect_state"];

        ExpectStateData data;
        data.state_machine = state_node["machine"].as<std::string>();
        data.state = state_node["state"].as<std::string>();

        step.data = data;

        // Parse timeout if present in expect_state block
        if (state_node["timeout"]) {
            step.timeout = state_node["timeout"].as<double>();
        }
    }
    else if (node["expect_transition"]) {
        step.type = StepType::EXPECT_TRANSITION;
        auto trans_node = node["expect_transition"];

        ExpectTransitionData data;
        data.state_machine = trans_node["machine"].as<std::string>();
        data.from_state = trans_node["from"].as<std::string>();
        data.to_state = trans_node["to"].as<std::string>();

        step.data = data;

        // Parse timeout if present in expect_transition block
        if (trans_node["timeout"]) {
            step.timeout = trans_node["timeout"].as<double>();
        }
    }

    if (node["timeout"]) {
        step.timeout = node["timeout"].as<double>();
    }

    return step;
}

TestCase YamlParser::parse_test_case(const YAML::Node& node) {
    TestCase test_case;

    test_case.name = node["name"].as<std::string>();

    if (node["description"]) {
        test_case.description = node["description"].as<std::string>();
    }

    if (node["steps"]) {
        for (const auto& step_node : node["steps"]) {
            test_case.steps.push_back(parse_step(step_node));
        }
    }

    if (node["setup"]) {
        for (const auto& step_node : node["setup"]) {
            test_case.setup.push_back(parse_step(step_node));
        }
    }

    if (node["teardown"]) {
        for (const auto& step_node : node["teardown"]) {
            test_case.teardown.push_back(parse_step(step_node));
        }
    }

    return test_case;
}

Fixture YamlParser::parse_fixture(const YAML::Node& node) {
    Fixture fixture;

    fixture.name = node["name"].as<std::string>();
    fixture.type = node["type"].as<std::string>();

    if (node["config"]) {
        for (const auto& kv : node["config"]) {
            fixture.config[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    return fixture;
}

TestSuite YamlParser::parse_file(const std::string& file_path) {
    LOG(INFO) << "Parsing test suite: " << file_path;

    YAML::Node root = YAML::LoadFile(file_path);

    // Get test_suite node
    YAML::Node suite_node = root["test_suite"];
    if (!suite_node) {
        throw std::runtime_error("Missing 'test_suite' key in YAML");
    }

    TestSuite suite;
    suite.name = suite_node["name"].as<std::string>();

    if (suite_node["description"]) {
        suite.description = suite_node["description"].as<std::string>();
    }

    if (suite_node["fixtures"]) {
        for (const auto& fixture_node : suite_node["fixtures"]) {
            suite.fixtures.push_back(parse_fixture(fixture_node));
        }
    }

    if (suite_node["setup"]) {
        for (const auto& setup_item : suite_node["setup"]) {
            // Handle both direct steps and actions array
            if (setup_item["actions"]) {
                for (const auto& step_node : setup_item["actions"]) {
                    suite.setup.push_back(parse_step(step_node));
                }
            } else {
                suite.setup.push_back(parse_step(setup_item));
            }
        }
    }

    if (suite_node["test_cases"]) {
        for (const auto& test_case_node : suite_node["test_cases"]) {
            suite.test_cases.push_back(parse_test_case(test_case_node));
        }
    }

    LOG(INFO) << "Parsed " << suite.test_cases.size() << " test case(s)";

    return suite;
}


} // namespace testing
} // namespace kuksa
