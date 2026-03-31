# cbftp - AI Agent Guide

## Project Overview

cbftp is an advanced multi-purpose FTP/FXP client written in C++ that focuses on efficient large-scale data spreading. It features a semi-graphical user interface built with ncurses and runs in a terminal.

- **License**: MIT License
- **Language**: C++11
- **Codebase Size**: ~63,000 lines of C++ code (192 .cpp files, 212 header files)
- **Primary Use Case**: FTP/FXP data spreading, file transfers, and site management

## Key Features

- **Spread Jobs**: Efficient many-to-many FTP server data spreading through FXP
- **Transfer Jobs**: Regular file transfer needs (FXP, Upload, Download)
- **Transfer Engine**: Intelligent scoring-based transfer scheduling
- **TLS Support**: AUTH TLS and TLS FXP support
- **IPv6 Support**: Full IPv6 connectivity and IPv6 FXP
- **Remote APIs**: HTTPS/JSON REST API and UDP API for remote commands
- **Encrypted Data**: AES-256-CBC encryption for the data file

## Architecture

### Directory Structure

```
src/                    # Main source code
├── core/               # Core event-driven I/O framework
│   ├── iomanager.cpp   # Socket management and event dispatching
│   ├── event*.cpp      # Event system
│   ├── polling*.h      # Platform-specific polling (epoll, kqueue, poll)
│   └── ...
├── ui/                 # Ncurses-based user interface
│   ├── screens/        # UI screens (browse, settings, etc.)
│   ├── *.cpp           # UI components and rendering
│   └── ...
├── http/               # HTTP/JSON REST API server
│   ├── *.cpp           # HTTP request/response handling
│   └── ...
├── ext/                # External libraries
│   ├── json.hpp        # JSON library (single header)
│   └── picohttpparser  # Minimal HTTP parser
└── tools/              # Utility tools
    ├── datafilecat.cpp    # Data file viewer
    └── datafilewrite.cpp  # Data file editor

examples/               # Example API scripts (Python, shell)
misc/                   # Miscellaneous scripts
```

### Core Components

1. **Engine** (`src/engine.cpp`): The transfer engine - calculates optimal transfers using scoring algorithms
2. **IOManager** (`src/core/iomanager.cpp`): Handles all socket interactions using epoll/kqueue/poll
3. **Site/SiteLogic**: Manages FTP site connections and state
4. **Race/TransferJob**: Job management for spread jobs and transfer jobs
5. **ScoreBoard**: Transfer scoring and optimization
6. **HTTPServer**: REST API server for remote control

### Threading Model

- Event-driven architecture with a main I/O thread
- Worker thread for CPU-intensive tasks
- Thread-safe communication via events and blocking queues
- UI is optional - cbftp can run headless (comment out `UI_PATH` in Makefile.inc)

## Build System

### Configuration

Key configuration is in `Makefile.inc`:

- `STATIC_SSL_PATH`: Path to static OpenSSL (optional)
- `UI_PATH`: Set to `src/ui` for UI support, empty for headless
- `DATA_FILE`: Path to data file (default: `"~/.cbftp/data"`)
- `CXXFLAGS`: Compiler flags (defaults to `-g -O0`)

### Dependencies

**Linux:**
- make, g++, libssl-dev, libncurses-dev

**BSD:**
- gmake, gcc11-devel, openssl, ncurses

**macOS:**
- Requires Homebrew OpenSSL

### Build Commands

```bash
# Standard build
make

# Parallel build (faster)
make -j8

# Debug build
make OPTFLAGS="-g -O0"

# Release build
make OPTFLAGS="-O3"

# Docker build
make docker-build
make docker-run

# Clean
make clean

# Count lines of code
make linecount
```

### Build Outputs

- `bin/cbftp` - Main binary
- `bin/cbftp-debug` - GDB wrapper script
- `bin/datafilecat` - Data file viewer tool
- `bin/datafilewrite` - Data file editor tool

## Code Style Guidelines

### Naming Conventions

- **Classes**: `PascalCase` (e.g., `SiteLogic`, `TransferJob`)
- **Methods**: `camelCase` (e.g., `getSite()`, `transferComplete()`)
- **Private members**: camelCase, no special prefix (context indicates access)
- **Constants/Enums**: `UPPER_CASE` or `ENUM_NAME`
- **Files**: lowercase with underscores (e.g., `site_logic.cpp`)

### Code Patterns

1. **Event-Driven Programming**: Most classes inherit from `Core::EventReceiver`
2. **Smart Pointers**: Extensive use of `std::shared_ptr` and `std::unique_ptr`
3. **Namespaces**: Core functionality in `Core::` namespace
4. **Forward Declarations**: Use `#pragma once` for headers

### Example Class Structure

```cpp
#pragma once

#include <memory>
#include <string>

#include "core/eventreceiver.h"

class SomeClass;

class MyClass : public Core::EventReceiver {
public:
  MyClass();
  ~MyClass();
  void doSomething(const std::string& param);
  
private:
  std::shared_ptr<SomeClass> member;
  int privateVar;
};
```

## Testing

**No automated test suite exists.** Testing is done manually:

1. Build the project: `make -j8`
2. Run the binary: `bin/cbftp`
3. Test functionality through the UI or API

### Debug Tools

- `bin/cbftp-debug` - Runs with GDB with SIGINT handling configured
- `misc/start_with_gdb.sh` - GDB wrapper script

## Security Considerations

1. **Data File Encryption**: The data file (`~/.cbftp/data`) can be encrypted with AES-256-CBC
   - Encryption key is the same as the API password
   - Can be enabled/disabled through global settings

2. **API Authentication**:
   - HTTPS/JSON API uses HTTP Basic auth with the API password
   - UDP API includes password in plaintext or encrypted

3. **TLS Support**:
   - Supports AUTH TLS and Implicit TLS
   - Uses self-signed certificates for REST API

4. **Data File Tools**:
   ```bash
   # View encrypted data file
   bin/datafilecat
   
   # Edit data file manually
   bin/datafilewrite
   
   # Or use OpenSSL directly
   openssl enc -d -aes-256-cbc -pbkdf2 -md sha256 -in ~/.cbftp/data
   ```

## API and External Scripts

### REST API

- **Port**: 55477 (configurable)
- **Protocol**: HTTPS with self-signed certificate
- **Auth**: HTTP Basic auth (username ignored, password required)

Example using curl:
```bash
curl -k -u :password https://localhost:55477/sites
curl -k -u :password -X POST https://localhost:55477/raw -d '{"command": "site help", "sites": ["SITE1"]}'
```

### UDP API

One-way commands for simple operations:
```bash
# Plaintext
echo -n "password raw SITE1 site help" > /dev/udp/127.0.0.1/55477

# Encrypted
echo -n "password raw SITE1 site help" | openssl enc -aes-256-cbc -md sha256 > /dev/udp/127.0.0.1/55477
```

### Python API Helper

See `examples/cbapi.py` for a Python module to interact with the API.

## Common Development Tasks

### Adding a New UI Screen

1. Create header in `src/ui/screens/mynewscreen.h`
2. Create implementation in `src/ui/screens/mynewscreen.cpp`
3. Add to `src/ui/screens/Makefile`
4. Include and use from appropriate existing screen

### Adding a New REST API Endpoint

1. Find appropriate handler in `src/` (e.g., `httpserver.cpp`)
2. Add route handling in the HTTP server implementation
3. Follow existing JSON patterns for request/response

### Adding a Core Feature

1. Consider event-driven design - what events should be triggered?
2. Inherit from `Core::EventReceiver` if the class needs I/O events
3. Use `Core::IOManager` for socket operations
4. Follow existing patterns for thread safety

## Important Implementation Details

### Slot Handling

Unlike traditional FTP clients, cbftp does not lock slots to UI windows. Slots are:
- Used and reused for any purpose at any time
- Locked only during single file transfers
- Managed globally by the transfer engine

### Transfer Engine Scoring

The engine calculates scores for potential transfers based on:
- Site speed vs other sites
- File size
- Spread job progress
- Percentage of files owned
- Target site priority

### Skiplists

Pattern matching for files/directories to skip:
- Supports wildcards (`*`, `?`) and regex
- Actions: Allow, Deny, Unique, Similar
- Scoped: "In spread job" vs "Allround"
- Hierarchy: Site → Section → Global

## Version Control

- Uses SVN for primary version control (legacy)
- Git mirror available
- Version string auto-generated from SVN revision or `.version` file

## Troubleshooting

### Unicode Display Issues
Ensure UTF-8 locale: `export LANG=en_US.UTF-8`

### OpenSSL Errors on macOS
Set `STATIC_SSL_PATH` in `Makefile.inc` to Homebrew OpenSSL path

### High CPU Usage
Adjust list frequency settings per site to use dynamic rates

## Resources

- **README**: Comprehensive user documentation with all features explained
- **API**: Complete REST/UDP API specification
- **Examples**: Python scripts demonstrating API usage
