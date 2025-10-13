/**
 * @file main.cpp
 * @brief Standalone test runner example - shows how to use the SDV testing library
 *
 * This is a reference implementation of a standalone YAML test runner.
 * It demonstrates how to:
 * - Parse YAML test suites
 * - Connect to KUKSA databroker
 * - Execute test steps (inject, expect, wait)
 * - Report results
 *
 * For production use, see: base-images/test-framework-v5
 */

#include <kuksa_cpp/testing/yaml_parser.hpp>
#include <kuksa_cpp/testing/kuksa_client_wrapper.hpp>
#include <kuksa_cpp/testing/test_runner.hpp>
#include <glog/logging.h>
#include <iostream>
#include <cstdlib>

using namespace sdv::testing;

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    FLAGS_colorlogtostderr = 1;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <test-suite.yaml> [--kuksa-url <url>]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Example:" << std::endl;
        std::cerr << "  " << argv[0] << " my_test.yaml --kuksa-url localhost:55555" << std::endl;
        return 1;
    }

    std::string test_file = argv[1];
    std::string kuksa_url = "localhost:55555";  // Default for local development

    // Parse command line args
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--kuksa-url" && i + 1 < argc) {
            kuksa_url = argv[++i];
        }
    }

    // Check for environment variables (Docker compatibility)
    if (const char* env_addr = std::getenv("KUKSA_ADDRESS")) {
        if (const char* env_port = std::getenv("KUKSA_PORT")) {
            kuksa_url = std::string(env_addr) + ":" + std::string(env_port);
        }
    }

    LOG(INFO) << "═══════════════════════════════════════════════════════════";
    LOG(INFO) << "SDV Standalone Test Runner - Example";
    LOG(INFO) << "═══════════════════════════════════════════════════════════";
    LOG(INFO) << "Test suite: " << test_file;
    LOG(INFO) << "KUKSA URL: " << kuksa_url;
    LOG(INFO) << "";

    try {
        // Parse test suite
        YamlParser parser;
        TestSuite suite = parser.parse_file(test_file);

        LOG(INFO) << "Loaded test suite: " << suite.name;
        LOG(INFO) << "Test cases: " << suite.test_cases.size();
        LOG(INFO) << "Fixtures: " << suite.fixtures.size();
        LOG(INFO) << "";

        // Connect to KUKSA
        auto client = std::make_shared<KuksaClientWrapper>(kuksa_url);
        if (!client->connect()) {
            LOG(ERROR) << "Failed to connect to KUKSA databroker at " << kuksa_url;
            LOG(ERROR) << "Make sure KUKSA databroker is running";
            return 1;
        }

        LOG(INFO) << "Connected to KUKSA databroker";
        LOG(INFO) << "";

        // Run tests
        TestRunner runner(client);
        TestSuiteResult result = runner.run_suite(suite);

        // Cleanup
        client->disconnect();

        // Print summary
        LOG(INFO) << "";
        LOG(INFO) << "═══════════════════════════════════════════════════════════";
        LOG(INFO) << "Test Results";
        LOG(INFO) << "═══════════════════════════════════════════════════════════";
        LOG(INFO) << "Total:  " << result.total;
        LOG(INFO) << "Passed: " << result.passed;
        LOG(INFO) << "Failed: " << result.failed;
        LOG(INFO) << "";

        // Return exit code based on results
        if (result.failed > 0) {
            LOG(ERROR) << "TESTS FAILED";
            return 1;
        }

        LOG(INFO) << "ALL TESTS PASSED";
        return 0;

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }
}
