# Standalone Test Runner Example

This example demonstrates how to build a standalone YAML test runner using the SDV testing library.

## Purpose

This is a **reference implementation** showing how to:
- Parse YAML test suites
- Connect to KUKSA databroker
- Execute test steps (inject, expect, wait, log)
- Report test results
- Handle errors gracefully

## When to Use This Pattern

Use this pattern when you need:
- **CI/CD test execution** - Standalone binary for Docker containers
- **Command-line testing** - Manual test execution without IDE
- **Automated test suites** - Run multiple test files sequentially
- **Integration testing** - Test applications without source code access

## When NOT to Use This Pattern

For local development with debugging, use the **Google Test integration** instead:
```cpp
#include <sdv/testing/gtest_integration.hpp>

class MyAppTest : public YamlTestFixture {
    // Your app runs here - set breakpoints!
};
```

See: [TESTING.md](../../TESTING.md#mode-2-google-test--local-debugging)

## Building

The example is built automatically when `BUILD_EXAMPLES=ON` and `WITH_TESTING=ON`:

```bash
cd libkuksa-cpp
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DWITH_TESTING=ON
make standalone_test_runner
```

## Usage

```bash
# Basic usage
./standalone_test_runner <test-suite.yaml>

# With custom KUKSA URL
./standalone_test_runner test.yaml --kuksa-url localhost:55555

# Using environment variables (Docker-friendly)
export KUKSA_ADDRESS=databroker
export KUKSA_PORT=55555
./standalone_test_runner test.yaml
```

## Example Test Suite

Create `example_test.yaml`:

```yaml
test_suite:
  name: "Example Test Suite"

  fixtures:
    - name: "battery_sensor"
      type: "periodic_publisher"
      config:
        path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
        value: "75.0"
        interval_ms: "1000"

  test_cases:
    - name: "Battery Level Check"
      steps:
        - log: "Testing battery sensor"

        - inject:
            path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
            value: 80.0
            actuator_mode: actual

        - wait: 0.5

        - expect:
            path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
            value: 80.0
            timeout: 2.0
```

Run it:
```bash
./standalone_test_runner example_test.yaml
```

## Exit Codes

- `0` - All tests passed
- `1` - One or more tests failed OR error occurred

## Production Usage

For production CI/CD pipelines, use **test-framework-v5** instead:
- Location: `base-images/test-framework-v5/`
- Docker-ready
- Optimized for automated testing
- Based on this example

## Implementation Details

The standalone test runner:

1. **Parses YAML** using `YamlParser`
2. **Connects to KUKSA** using `KuksaClientWrapper`
3. **Executes test steps** using `TestRunner`:
   - `inject` - Set values in databroker
   - `expect` - Wait for expected values
   - `wait` - Pause execution
   - `log` - Print messages
4. **Reports results** - Pass/fail counts

## Key Components Used

```cpp
#include <sdv/testing/yaml_parser.hpp>       // YAML parsing
#include <sdv/testing/kuksa_client_wrapper.hpp> // KUKSA connection
#include <sdv/testing/test_runner.hpp>        // Test execution
```

## Customization

You can extend this example to:
- Add custom test step types
- Implement fixture management
- Add parallel test execution
- Generate test reports in different formats
- Integrate with test result databases

## Comparison with test-framework-v5

| Feature | This Example | test-framework-v5 |
|---------|-------------|-------------------|
| Purpose | Reference/Learning | Production CI/CD |
| Default KUKSA URL | localhost:55555 | databroker:55555 |
| Docker optimized | No | Yes |
| Error handling | Basic | Comprehensive |
| Logging | Standard | Structured |
| Installation | examples/ | /usr/local/bin/ |

## See Also

- [TESTING.md](../../TESTING.md) - Complete testing guide
- [Test Models](../../include/sdv/testing/test_models.hpp) - Data structures
- [YAML Parser](../../include/sdv/testing/yaml_parser.hpp) - YAML format
- [test-framework-v5](../../../../base-images/test-framework-v5/) - Production version
