# Climate Control Integration Test

## Overview

This integration test demonstrates the **full test framework capabilities** for testing complex applications that interact with KUKSA databroker.

The test architecture includes:
1. **KUKSA Databroker** (Docker container) - VSS signal database
2. **Fixture Runner** (Docker container) - Simulates external sensors/actuators
3. **Climate Control App** (Test subject) - Your application under test

## Test Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     Docker Environment                        │
│                                                               │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Docker Network (test-network-<timestamp>)          │   │
│  │                                                      │   │
│  │  ┌───────────────────┐                              │   │
│  │  │  KUKSA Databroker │                              │   │
│  │  │  Port: 55555      │◄─────┐                      │   │
│  │  └───────────────────┘      │                      │   │
│  │           ▲                  │                      │   │
│  │           │                  │                      │   │
│  │  ┌────────┴──────────┐      │                      │   │
│  │  │  Fixture Runner    │      │                      │   │
│  │  │  - Battery Sensor  │──────┘                      │   │
│  │  │  - HVAC Actuator   │                             │   │
│  │  └───────────────────┘                              │   │
│  │                                                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ▲                                   │
└──────────────────────────┼───────────────────────────────────┘
                           │
            ┌──────────────┴───────────────┐
            │  Climate Control App          │
            │  (Test Subject - Host Process)│
            │  Connects to localhost:55555  │
            └──────────────────────────────┘
```

## What This Test Demonstrates

### 1. YAML-Based Test Definition
The test is defined declaratively in `yaml/climate_control_battery_protection.yaml`:
- Fixture configuration (sensors/actuators to simulate)
- Test cases with setup, steps, and teardown
- Signal injection and expectations
- Timing and synchronization

### 2. Fixture Runner
Simulates external vehicle components:
- **Battery Sensor**: Publishes voltage updates to KUKSA
- **HVAC Actuator**: Simulates HVAC control responses

### 3. Test Scenarios
Three test cases demonstrate battery protection:

#### Test 1: Normal Operation
- Battery voltage is normal (24.5V)
- HVAC remains active
- ✓ Verifies system allows normal operation

#### Test 2: Battery Protection Triggers
- Battery drops to critical (23.0V)
- System should shut down HVAC
- ✓ Verifies protection logic activates

#### Test 3: System Recovery
- Battery recovers to safe level (25.0V)
- HVAC can be reactivated
- ✓ Verifies system recovers properly

## Prerequisites

### Required Dependencies
- Docker (running)
- KUKSA Databroker image: `ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0`
- Fixture runner image: `sdv-fixture-runner:latest` (custom)

### Pull KUKSA Image
```bash
docker pull ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0
```

### Build Fixture Runner
**Note**: The fixture runner is a placeholder in this test. You need to implement it or mock the fixtures manually.

For this test to work fully, you have two options:

#### Option 1: Skip Fixtures (Manual Testing)
Remove the `fixtures:` section from the YAML and manually inject signals using the test framework's inject capabilities.

#### Option 2: Implement Fixture Runner
Create a simple fixture runner that:
- Reads fixture config from JSON
- Connects to KUKSA databroker
- Publishes sensor values at specified intervals
- Responds to actuator commands

## Building the Test

```bash
# From project root
mkdir build && cd build
cmake ..
cmake --build . --target test_climate_control_integration
```

## Running the Test

### Run Directly
```bash
# From build directory
./tests/integration/test_climate_control_integration
```

### Run with CTest
```bash
# From build directory
ctest -R test_climate_control_integration -V
```

### Using External KUKSA Instance
Skip Docker container management by pointing to existing KUKSA:

```bash
export KUKSA_ADDRESS="remote-server:55555"
./tests/integration/test_climate_control_integration
```

**Note**: You'll need to manage fixtures separately when using external KUKSA.

## Expected Output

```
Starting climate control system...
Climate control system started

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Running: Normal Operation with Good Battery
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

=== Test 1: Normal battery voltage ===
✓ HVAC remains active with good battery

[       OK ] ClimateControlIntegrationTest.BatteryProtectionTriggersOnLowVoltage

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Running: Battery Protection Triggers on Low Voltage
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

=== Test 2: Battery voltage drops to critical ===
✓ Battery protection triggered - HVAC shut down

[       OK ] ClimateControlIntegrationTest.BatteryProtectionTriggersOnLowVoltage

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Running: System Recovers When Battery Voltage Improves
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

=== Test 3: Battery recovers to safe level ===
✓ System recovered - HVAC operational

Stopping climate control system...
Climate control system stopped
Stopping test containers...
```

## Files Structure

```
tests/integration/
├── test_climate_control_integration.cpp   # GTest fixture and main()
├── yaml/
│   └── climate_control_battery_protection.yaml  # Test definition
└── README_CLIMATE_CONTROL_TEST.md         # This file

examples/climate_control/
├── climate_control.hpp                    # Climate control system
├── climate_control.cpp                    # Implementation
├── vss_extensions.json                    # Custom VSS signals
└── ...
```

## Customizing the Test

### Change KUKSA Port
Override `GetKuksaPort()` in the test fixture:

```cpp
class ClimateControlIntegrationTest : public sdv::testing::YamlTestFixture {
protected:
    uint16_t GetKuksaPort() const override {
        return 55557;  // Use different port
    }
    // ...
};
```

### Add More Test Cases
Edit `yaml/climate_control_battery_protection.yaml` and add new test cases:

```yaml
test_cases:
  - name: Your New Test Case
    description: What this test verifies

    steps:
      - type: inject
        path: Vehicle.Signal.Path
        value: 123.45

      - type: expect
        path: Vehicle.Other.Signal
        value: true
        timeout: 5.0
```

### Use Custom VSS Schema
The test already uses `vss_extensions.json` from the climate control example. To use a different schema:

```cpp
std::string GetVssSchema() const override {
    return "path/to/your/custom_vss.json";
}
```

## Troubleshooting

### Docker Not Available
```
Error: Docker is not available or not running.
```
**Solution**: Start Docker daemon or use `KUKSA_ADDRESS` environment variable

### Port Already in Use
```
Error: Port 55555 is already in use.
```
**Solution**: Override `GetKuksaPort()` or stop service on port 55555

### Fixture Runner Not Found
```
Error: Failed to start fixture runner
```
**Solution**:
- Remove `fixtures:` section from YAML, or
- Implement the fixture runner container

### Test Timeout
If tests hang or timeout:
- Check KUKSA container logs: `docker logs <container-name>`
- Verify climate control app connects successfully
- Increase wait times in YAML test steps

## Integration Test Best Practices

### 1. Keep Tests Independent
Each test case should:
- Have its own setup/teardown
- Not depend on other test cases
- Clean up state properly

### 2. Use Descriptive Names
```yaml
- name: Clear, Descriptive Test Name
  description: What behavior this test verifies and why it matters
```

### 3. Add Logging
Use `type: log` steps to document test progress:
```yaml
- type: log
  message: "=== Starting battery protection scenario ==="
```

### 4. Set Appropriate Timeouts
Give operations enough time to complete:
```yaml
- type: expect
  path: Vehicle.Signal
  value: expected_value
  timeout: 5.0  # Adjust based on your system
```

## Next Steps

1. **Implement fixture runner** - Create a reusable container for simulating sensors/actuators
2. **Add more test cases** - Expand coverage for fuel protection, engine management, etc.
3. **Integrate with CI/CD** - Run these tests automatically on commits
4. **State machine testing** - Add state transition verification using `expect_state` and `expect_transition`

## Related Documentation

- [Test Framework Overview](../../include/kuksa_cpp/testing/README.md)
- [YAML Test Format](../../include/kuksa_cpp/testing/YAML_FORMAT.md)
- [Climate Control Example](../../examples/climate_control/README.md)
- [KUKSA Databroker Docs](https://github.com/eclipse-kuksa/kuksa-databroker)
