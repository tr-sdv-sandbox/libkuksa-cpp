# SDV Testing Library

The SDV C++ SDK includes a comprehensive testing library that supports both Docker-based CI testing and local Google Test development with full debugging support.

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      SDV Testing Library                      │
│                     (sdk/cpp/testing/)                        │
├──────────────────────────────────────────────────────────────┤
│                                                                │
│  Core Components:                                              │
│  • YAML Parser - Parse declarative test definitions           │
│  • Test Runner - Execute test steps                           │
│  • KUKSA Client Wrapper - Interact with databroker            │
│  • Test Models - Data structures for tests                    │
│  • Google Test Integration - Manage Docker + app lifecycle    │
│                                                                │
└──────────────────────────────────────────────────────────────┘
                      ▲                    ▲
                      │                    │
         ┌────────────┴─────┐   ┌─────────┴──────────┐
         │                  │   │                    │
    ┌────▼─────┐      ┌────▼───▼────┐         ┌─────▼──────┐
    │test-     │      │ Your Google │         │  CI/CD     │
    │framework │      │    Tests    │         │ Pipeline   │
    │   -v5    │      │             │         │            │
    │ (Docker) │      │ (Debuggable)│         │  (Docker)  │
    └──────────┘      └─────────────┘         └────────────┘
```

## Two Usage Modes

### Mode 1: Docker-Only (CI/CD, headless testing)

Used by `test-framework-v5` Docker image and CI pipelines.

```bash
# Start all components in Docker
./run-tests-v5.sh --image my-app:latest --tests ./tests/

# Everything runs in containers
# Logs are collected and displayed
# No debugging, but perfect for CI
```

**Use Cases:**
- Automated CI/CD pipelines
- Integration testing in production-like environment
- Testing without local SDK installation
- Reproducible test environments

### Mode 2: Google Test + Local Debugging

Your application runs natively, fully debuggable in IDE.

```cpp
#include <sdv/testing/gtest_integration.hpp>

class MyAppTest : public YamlTestFixture {
protected:
    void StartTestSubject() override {
        // Your code runs HERE - set breakpoints!
        my_app_ = std::make_unique<MyApp>("localhost:55555");
        my_app_->start();
    }

    void StopTestSubject() override {
        my_app_->stop();
    }

    std::unique_ptr<MyApp> my_app_;
};

TEST_F(MyAppTest, RunAllTests) {
    RunYamlTestSuite("tests/my_test.yaml");
}
```

**Use Cases:**
- Local development and debugging
- Step-through code with breakpoints
- Inspect variables and state
- Rapid iteration during development

## Component Startup Order

Critical for proper testing:

```
1. KUKSA Databroker     (Foundation - all others connect to this)
   └─> localhost:55555

2. Fixture Runner       (Simulates hardware - battery sensor, etc)
   └─> Publishes sensor data
   └─> Responds to provider requests

3. Your Application     (Test subject)
   └─> Registers as provider for actuators
   └─> Subscribes to sensors
   └─> Implements business logic

4. Test Framework       (Sends test commands)
   └─> inject: Sets values via KUKSA
   └─> expect: Verifies values via KUKSA
   └─> wait: Allows time for processing
```

## YAML Test Format

Tests are defined declaratively in YAML:

```yaml
test_suite:
  name: "My Application Tests"

  fixtures:
    - name: "battery_sensor"
      type: "periodic_publisher"
      config:
        path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
        value: "75.0"
        interval_ms: "1000"

  test_cases:
    - name: "High Battery Enables Feature"
      steps:
        - inject:
            path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
            value: 80.0
        - inject:
            path: "Vehicle.MyFeature.Enabled"
            value: true
            actuator_mode: target  # Routes through provider
        - wait: 1
        - expect:
            path: "Vehicle.MyFeature.Enabled"
            value: true
            timeout: 5
```

## Test Step Types

### inject
Injects a value into KUKSA databroker.

- `actuator_mode: target` - Uses `Actuate()` RPC (routes through provider stream)
- `actuator_mode: actual` - Uses `PublishValue()` RPC (standalone publish)

```yaml
- inject:
    path: "Vehicle.Cabin.HVAC.IsAirConditioningActive"
    value: true
    actuator_mode: target  # Your provider receives this via stream
```

### expect
Waits for a value to match (with timeout).

```yaml
- expect:
    path: "Vehicle.Cabin.HVAC.IsAirConditioningActive"
    value: true
    timeout: 5  # seconds
```

### wait
Pauses for a duration.

```yaml
- wait: 1.5  # seconds
```

### log
Outputs a message (useful for debugging).

```yaml
- log: "Starting AC activation test"
```

### expect_state (TODO)
Verifies state machine is in expected state.

```yaml
- expect_state:
    machine: "ClimateControl"
    state: "AC_ACTIVE"
```

### expect_transition (TODO)
Verifies state machine transitioned.

```yaml
- expect_transition:
    machine: "ClimateControl"
    from: "AC_OFF"
    to: "AC_ACTIVE"
```

## Fixtures

Fixtures simulate hardware components:

### periodic_publisher
Publishes a value periodically.

```yaml
fixtures:
  - name: "battery_sensor"
    type: "periodic_publisher"
    config:
      path: "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current"
      value: "75.0"
      interval_ms: "1000"
```

### provider
Acts as a KUKSA provider (responds to actuation requests).

```yaml
fixtures:
  - name: "door_actuator"
    type: "provider"
    config:
      path: "Vehicle.Body.Doors.Row1.DriverSide.IsOpen"
      initial_value: "false"
```

## Google Test Integration API

### YamlTestFixture Base Class

```cpp
class YamlTestFixture : public ::testing::Test {
protected:
    // YOU IMPLEMENT THESE:
    virtual void StartTestSubject() = 0;  // Start your app
    virtual void StopTestSubject() = 0;   // Stop your app

    // YOU CALL THESE:
    void RunYamlTestSuite(const std::string& yaml_path);
    void RunYamlTestCase(const std::string& yaml_path, const std::string& test_name);

    // ADVANCED API:
    std::shared_ptr<TestRunner> GetTestRunner();
    std::shared_ptr<KuksaClientWrapper> GetKuksaClient();
};
```

### Example Test

```cpp
class MyAppTest : public YamlTestFixture {
protected:
    void StartTestSubject() override {
        app_ = std::make_unique<MyApp>("localhost:55555");
        app_thread_ = std::thread([this]() { app_->run(); });
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    void StopTestSubject() override {
        app_->stop();
        if (app_thread_.joinable()) app_thread_.join();
    }

private:
    std::unique_ptr<MyApp> app_;
    std::thread app_thread_;
};

TEST_F(MyAppTest, AllTests) {
    RunYamlTestSuite("tests/my_test.yaml");
}

TEST_F(MyAppTest, SpecificTest) {
    RunYamlTestCase("tests/my_test.yaml", "Test Name");
}
```

## Debugging Workflow

1. **Write YAML test** - Define expected behavior
2. **Create Google Test** - Extend `YamlTestFixture`
3. **Set breakpoints** - In your application code
4. **Run in IDE** - Google Test starts Docker, runs your app locally
5. **Debug** - Step through, inspect variables
6. **Fix code** - Iterate quickly

## Building

### SDK with Testing Library

```bash
cd sdk/cpp
./build.sh
sudo make install  # Installs to /usr/local
```

### Your Application with Tests

```cmake
find_package(GTest REQUIRED)

# Link against SDK testing library
add_executable(my_app_test tests/my_app_test.cpp)
target_link_libraries(my_app_test
    my_app  # Your application code
    sdv::testing  # SDK testing library
    GTest::gtest_main
)
```

## Best Practices

1. **Start simple** - One fixture, one test case
2. **Use descriptive names** - Test cases should explain intent
3. **Test state machines** - Use expect_state/expect_transition
4. **Keep tests fast** - Use minimal wait times
5. **Debug locally** - Google Test + IDE is fastest
6. **Verify in CI** - Docker mode ensures reproducibility

## Troubleshooting

### "Failed to connect to KUKSA"
- Check databroker is running: `docker ps | grep databroker`
- Check port 55555 is not in use: `nc -z localhost 55555`

### "Fixture runner stopped unexpectedly"
- Check fixture config is valid
- View fixture logs: `docker logs fixture-runner-*`

### "Test hangs on expect"
- Increase timeout value
- Check your app is actually updating the databroker
- Use manual test with GetKuksaClient() to debug

### "Cannot find sdv::testing"
- Rebuild and install SDK: `cd sdk/cpp && ./build.sh && sudo make install`
- Check CMake finds it: `find_package(sdv_testing REQUIRED)`

## Files in SDK

```
sdk/cpp/
├── include/sdv/testing/
│   ├── test_models.hpp           # Data structures
│   ├── yaml_parser.hpp           # YAML parsing
│   ├── kuksa_client_wrapper.hpp  # KUKSA interaction
│   ├── test_runner.hpp           # Test execution
│   └── gtest_integration.hpp     # Google Test helpers
├── src/testing/
│   ├── yaml_parser.cpp
│   ├── kuksa_client_wrapper.cpp
│   ├── test_runner.cpp
│   └── gtest_integration.cpp
└── TESTING.md (this file)
```

## Examples

- `examples/cpp-climate-control/tests/climate_control_gtest.cpp` - Full example
- `examples/cpp-climate-control/tests/simple_ac_test.yaml` - YAML test definition

## Next Steps

1. Read the example: `examples/cpp-climate-control/tests/`
2. Create your own YAML test
3. Extend `YamlTestFixture` for your app
4. Run and debug!
