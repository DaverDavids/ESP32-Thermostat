# AGENTS.md - ESP32-Thermostat Project Guidelines

## Build, Lint, and Test Commands

### Build Commands
- **PlatformIO**: `pio run` (if using PlatformIO)
- **Arduino CLI**: `arduino-cli compile --fqbn esp32:esp32:esp32 ESP32-Thermostat.ino`
- **Arduino IDE**: Verify and upload via IDE interface
- **Direct ESP-IDF**: Not directly applicable as this uses Arduino core

### Linting
- **Cppcheck**: `cppcheck --enable=all --std=c++11 ESP32-Thermostat.ino`
- **Clang-format**: `clang-format -i ESP32-Thermostat.ino` (requires .clang-format config)
- **Arduino Linter**: Available via PlatformIO or VS Code extensions
- **Manual Review**: Check for consistent 2-space indentation and naming conventions

### Testing
- **Unit Testing**: No formal test framework detected in repository
- **Manual Testing**: Upload to ESP32 and verify functionality via web interface or serial monitor
- **Simulation**: Use Wokwi or Tinkercad for circuit simulation before hardware testing
- **Single Test Execution**: Not applicable without test framework; verify specific functionality manually
- **Compilation Check**: `arduino-cli verify --fqbn esp32:esp32:esp32 ESP32-Thermostat.ino`

### Development Workflow
1. Modify code in ESP32-Thermostat.ino
2. Verify syntax with Arduino IDE (`Verify`) or PlatformIO (`pio run`)
3. Upload to ESP32 device (`Upload` in IDE or `pio run --target upload`)
4. Test functionality via web interface (http://thermostat.local) or serial monitor
5. Iterate based on results
6. For web interface changes, verify HTML in html.h renders correctly

## Code Style Guidelines

### File Organization
- Single main sketch file (ESP32-Thermostat.ino)
- External HTML stored in html.h (progmem array)
- No separate header/source files for logic
- Constants and configurations grouped at top of file
- Related functionality sectioned with comment headers

### Imports and Includes
- Group standard libraries first (Arduino.h, WiFi.h, etc.)
- Group third-party libraries next (Adafruit_*, WebServer, etc.)
- Local includes last ("html.h", "Secrets.h")
- Each include on separate line
- No blank lines between related includes
- Local includes use double quotes ("html.h")

### Formatting
- **Indentation**: 2 spaces (consistently used throughout codebase)
- **Braces**: Mixed style - Allman (brace on new line) for function definitions, K&R (brace on same line) for code blocks
- **Line Length**: Aim for <100 characters, but flexible for readability
- **Section Headers**: Use comments with `// ─── Section Name ─────────────────────────────────────`
- **Blank Lines**: Use to separate logical sections within functions and between major code blocks
- **Trailing Whitespace**: Remove trailing whitespace on lines

### Naming Conventions
- **Constants**: ALL_CAPS_WITH_UNDERSCORES (e.g., `PIN_MOSFET`, `SAMPLE_MS`, `WIFI_TIMEOUT_MS`)
- **Defines/Macros**: ALL_CAPS_WITH_UNDERSCORES (e.g., `#define MOSFET_WRITE(on)`)
- **Variables**: camelCase (e.g., `setpoint`, `currentTemp`, `manualOverride`, `histHead`)
- **Functions**: camelCase (e.g., `updateDisplay()`, `readTempC()`, `controlLoop()`, `setup()`)
- **Types/Structs**: PascalCase (e.g., `BtnState`, `Preferences`, `Adafruit_INA219`)
- **Enums**: PascalCase for name, ALL_CAPS for values (e.g., `BtnPhase { BTN_IDLE, BTN_PENDING, BTN_HELD }`)
- **Pins**: Prefixed with `PIN_` (e.g., `PIN_MOSFET`, `PIN_SDA`, `PIN_BTN_UP`)
- **Timing Constants**: Suffixed with `_MS` for milliseconds (e.g., `DEBOUNCE_MS`, `RAMP_DELAY_MS`)
- **Frequency/Rate**: Suffixed with `_HZ` if applicable (none observed)
- **Boolean Variables**: Prefixed with `is`, `has`, `should`, or descriptive state (e.g., `outputOn`, `manualOverride`, `apMode`)
- **Time Variables**: Prefixed with `last` or `next` for timing (e.g., `lastSample`, `nextFire`)
- **Counters/Indices**: Use descriptive names like `histHead`, `histCount`, `i` for loop indices

### Types and Variables
- **Explicit Types**: Use fixed-width integers where appropriate (uint8_t for pins/counts, uint32_t for time/millis)
- **Floating Point**: Use `float` for sensor readings and calculations (adequate precision for thermostat)
- **Boolean**: Use `bool` for true/false values
- **Strings**: Use `String` for dynamic text (SSID, passwords), `const char*` for constants
- **Avoid**: Magic numbers; use named constants instead (observed throughout codebase)
- **Initialization**: Initialize variables at declaration when possible (seen in struct initialization)
- **Scope**: Minimize variable scope; declare near first use
- **Constants**: Use `const` for values that don't change after initialization

### Error Handling
- **Serial Debugging**: Use `Serial.print()`/`Serial.println()` for diagnostics
- **Debug Macros**: Use `DBG(x)` and `DBGLN(x)` macros controlled by `DEBUG` flag (lines 15-22)
- **Hardware Checks**: Verify device initialization (e.g., `if (!ina219.begin())` line 132)
- **Fallback Behavior**: Provide defaults when hardware not found (continues with limited function)
- **WiFi Handling**: Connection timeouts (`WIFI_TIMEOUT_MS`) and retry logic with AP fallback
- **Bounds Checking**: Use `min()`, `max()`, `constrain()` for value limits (seen in button handling)
- **Array Safety**: Use modulus operator for circular buffers (e.g., `histHead = (histHead + 1) % HIST_SIZE`)
- **Division by Zero**: Check denominators before division (seen in calibration route)
- **Network Operations**: Check return values and handle failures gracefully

### Comments and Documentation
- **Section Comments**: Use `// ─── Section Name ─────────────────────────────────────` for major sections (observed throughout)
- **Function Comments**: Brief description before complex functions (minimal in current code)
- **Inline Comments**: Explain non-obvious logic or calculations (especially in `readTempC()`)
- **TODO Comments**: Use `// TODO:` for future work (none observed in current code)
- **Pin Assignments**: Comment explaining purpose and active state (e.g., MOSFET logic lines 32-34)
- **Calculation Comments**: Explain formulas and conversions (especially in `readTempC()` lines 354-366)
- **Section Headers**: Maintain consistent format with section name centered in dashes
- **Block Comments**: Use for explaining complex algorithms or configurations

### Specific Patterns Observed
1. **Pin Definitions**: Grouped with comments explaining usage (lines 24-34)
2. **Configuration Constants**: Grouped by subsystem (WiFi, Timing, Button, etc.) (lines 42-64)
3. **Global Variables**: Declared after includes and constants (lines 65-96)
4. **Forward Declarations**: Grouped before setup() function (lines 109-115)
5. **Setup Function**: Hardware initialization in logical order (lines 117-179)
6. **Loop Function**: Non-blocking timing checks with millis() (lines 182-239)
7. **Button State Machine**: Encapsulated in struct with phase tracking (lines 97-107)
8. **Preferences Handling**: Load/save pairs for persistent storage (lines 411-433)
9. **Web Routes**: Lambda functions for HTTP handlers (lines 436-512)
10. **Temperature Reading**: Oversampling and calibration applied (lines 354-366)
11. **Display Update**: Efficient partial updates only when needed (lines 252-284)
12. **Control Loop**: Simple bang-bang control with hysteresis (lines 368-373)

### Security Considerations
- **Credentials**: Stored in separate `Secrets.h` file (referenced line 12, not shown in repo)
- **WiFi**: Uses WPA2-PSK, configurable SSID/PSK via web interface (/wifi endpoint)
- **OTA**: ArduinoOTA enabled with hostname for secure updates
- **Network**: mDNS for local discovery (thermostat.local), DNS captive portal in AP mode
- **Web Server**: Basic authentication not implemented; consider for production use
- **Calibration**: Web endpoints should be secured in production environments

### Performance Considerations
- **Non-blocking Design**: Uses `millis()` for timing instead of `delay()` (throughout loop)
- **Efficient Updates**: Only updates display when needed (`DISPLAY_MS` interval, line 50)
- **Sensor Sampling**: Fixed interval (`SAMPLE_MS`) with averaging for stability (line 49)
- **History Buffer**: Circular buffer for temperature history to minimize RAM usage (lines 87-91)
- **Button Debouncing**: Software debounce with configurable timing (line 53)
- **Ramping Algorithm**: Accelerating button hold for faster adjustment (lines 56-63)
- **String Usage**: Minimize String object creation in loops to prevent fragmentation
- **Web Responses**: Prefer concise responses to reduce bandwidth and processing time
- **Display Updates**: Only update changed values to minimize OLED wear

### Best Practices from Codebase
1. **Modular Sections**: Clear separation of concerns with comment headers
2. **Consistent Indentation**: 2 spaces throughout (verified in examination)
3. **Descriptive Names**: Variables and functions clearly indicate purpose
4. **Hardware Abstraction**: Pin writes wrapped in macros (`MOSFET_WRITE`) for clarity
5. **Configuration Centralization**: All tunable parameters at top of file
6. **State Management**: Explicit state variables for UI and operation modes
7. **Fallback Mechanisms**: AP mode when STA fails, manual override for safety
8. **Persistence**: User preferences saved across reboots using Preferences library
9. **Web Interface**: Comprehensive status and control endpoints for monitoring
10. **Debug Control**: Conditional compilation for debug output via `DEBUG` flag
11. **Resource Management**: Proper cleanup in setup() and efficient resource usage
12. **Interrupt Safety**: Minimal work in ISRs (none present, but good practice noted)
13. **Watchdog Friendly**: No blocking delays in main loop that would trigger watchdog
14. **Memory Awareness**: Static allocation where possible, minimal dynamic allocation
15. **Consistency**: Follow established patterns in the codebase for new additions

### HTML Storage Guidelines
- HTML content stored in `html.h` as PROGMEM array to save RAM
- Use `F()` macro for string literals when possible
- Keep HTML concise to minimize flash usage
- Ensure proper escaping for web safety
- Consider template approach for dynamic content rather than full HTML regeneration
