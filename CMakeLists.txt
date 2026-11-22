cmake_minimum_required(VERSION 3.21)
# The version of Lemuri: the app, the backend, the packages and the release tags (v0.1.0) all take it from here.
project(lemuri VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# For clang-tidy and the editors: build/compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings are the same everywhere; LEMURI_WERROR (on in CI) makes them errors.
option(LEMURI_WERROR "Treat warnings as errors" OFF)
set(LEMURI_WARNINGS -Wall -Wextra -Wshadow=local)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(LEMURI_WARNINGS -Wall -Wextra -Wshadow-uncaptured-local)
endif()
if(LEMURI_WERROR)
    list(APPEND LEMURI_WARNINGS -Werror)
endif()
# LEMURI_SANITIZE=address (with undefined) or thread: everything built with it, tests included.
set(LEMURI_SANITIZE "" CACHE STRING "address or thread")
if(LEMURI_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
elseif(LEMURI_SANITIZE STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
# LEMURI_FUZZ (clang): every library instrumented for libFuzzer and ASan; the targets are at the end.
option(LEMURI_FUZZ "Build the fuzz targets (needs clang)" OFF)
if(LEMURI_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
# LEMURI_COVERAGE: gcov data for gcovr (tests/coverage.sh).
option(LEMURI_COVERAGE "Measure test coverage" OFF)
if(LEMURI_COVERAGE)
    add_compile_options(--coverage -O0)
    add_link_options(--coverage)
endif()

# 6.4: the Qt of Ubuntu 24.04, the oldest system the Linux package is built for.
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

# Everything but main(): the app and the tests link it.
add_library(lemuri-ui STATIC
    src/app/Backend.cpp
    src/app/Instance.cpp
    src/app/Model.cpp
    src/app/Shots.cpp
    src/app/Startup.cpp
    src/app/Texts.cpp
    src/widgets/Chrome.cpp
    src/widgets/Icons.cpp
    src/widgets/Style.cpp
    src/widgets/Ui.cpp
    src/terminal/Ghost.cpp
    src/terminal/TabStrip.cpp
    src/terminal/TerminalView.cpp
    src/window/Actions.cpp
    src/window/HostTree.cpp
    src/window/MainWindow.cpp
    src/window/Splash.cpp
    src/window/Views.cpp
    src/dialogs/Settings.cpp
    src/dialogs/Team.cpp
)
target_include_directories(lemuri-ui PUBLIC src)
target_compile_options(lemuri-ui PRIVATE ${LEMURI_WARNINGS})
target_link_libraries(lemuri-ui PUBLIC Qt6::Widgets Qt6::Network PkgConfig::VTERM)
# On X11 the window asks to be brought forward as a pager would (see Instance::activate).
if(UNIX AND NOT APPLE)
    pkg_check_modules(XCB IMPORTED_TARGET xcb)
    if(XCB_FOUND)
        target_link_libraries(lemuri-ui PUBLIC PkgConfig::XCB)
        target_compile_definitions(lemuri-ui PRIVATE LEMURI_XCB)
    endif()
endif()

add_executable(lemuri src/main.cpp resources.qrc)

# The backend: lemuri-core, started by the app, JSON lines on its standard input and output (backend/PROTOCOL.md).
# Qt Core and Network only, no windows; OpenSSL and libargon2 for the vault and the envelopes.
find_package(OpenSSL 3.0 REQUIRED)
pkg_check_modules(ARGON2 REQUIRED IMPORTED_TARGET libargon2)
pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh>=0.10)
add_library(lemuri-backend-parts OBJECT
    backend/Core.cpp
    backend/base/Files.cpp
    backend/base/I18n.cpp
    backend/base/Legacy.cpp
    backend/base/Log.cpp
    backend/base/Net.cpp
    backend/base/Prefs.cpp
    backend/base/Runtime.cpp
    backend/vault/Crypto.cpp
    backend/vault/Vault.cpp
    backend/vault/VaultAccess.cpp
    backend/hosts/Folders.cpp
    backend/hosts/Host.cpp
    backend/hosts/Hosts.cpp
    backend/hosts/Jumps.cpp
    backend/hosts/SshConfig.cpp
    backend/ssh/Sftp.cpp
    backend/ssh/Ssh.cpp
    backend/sessions/LocalShell.cpp
    backend/sessions/Sessions.cpp
    backend/sessions/Transfers.cpp
    backend/monitoring/Metrics.cpp
    backend/monitoring/Monitor.cpp
    backend/monitoring/Probe.cpp
    backend/monitoring/SecurityAudit.cpp
    backend/sharing/ContactNames.cpp
    backend/sharing/Envelope.cpp
    backend/sharing/Lan.cpp
    backend/sharing/Parcel.cpp
    backend/sharing/Team.cpp
    backend/backup/Backup.cpp
    backend/backup/SshFolder.cpp
    backend/backend.qrc
)
target_include_directories(lemuri-backend-parts PUBLIC backend)
target_compile_options(lemuri-backend-parts PRIVATE ${LEMURI_WARNINGS})
target_compile_definitions(lemuri-backend-parts PUBLIC LEMURI_VERSION="${PROJECT_VERSION}")
target_link_libraries(lemuri-backend-parts PUBLIC Qt6::Core Qt6::Network OpenSSL::Crypto PkgConfig::ARGON2 PkgConfig::LIBSSH)
if(NOT APPLE)
    target_link_libraries(lemuri-backend-parts PUBLIC util)  # forkpty
endif()
# The updater installs as root: the package's backend has no way to give it another server, other keys or another
# install command. Only the tests' backend (lemuri-core-testing) reads them from LEMURI_UPDATE_API, _SIGNERS, _INSTALL.
add_library(lemuri-backend STATIC backend/updates/Updates.cpp)
target_compile_options(lemuri-backend PRIVATE ${LEMURI_WARNINGS})
target_link_libraries(lemuri-backend PUBLIC lemuri-backend-parts)
add_executable(lemuri-core backend/main.cpp)
target_compile_options(lemuri-core PRIVATE ${LEMURI_WARNINGS})
target_link_libraries(lemuri-core PRIVATE lemuri-backend)
target_compile_options(lemuri PRIVATE ${LEMURI_WARNINGS})
target_link_libraries(lemuri PRIVATE lemuri-ui)

# Tests: ctest --test-dir build (no screen needed).
option(LEMURI_TESTS "Build the tests" ON)
if(LEMURI_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t TextsTest TerminalViewTest ModelTest)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE lemuri-ui Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    # The backend's own pieces, without the app.
    add_library(lemuri-backend-testing STATIC backend/updates/Updates.cpp)
    target_compile_options(lemuri-backend-testing PRIVATE ${LEMURI_WARNINGS})
    target_compile_definitions(lemuri-backend-testing PRIVATE LEMURI_TEST_HOOKS)
    target_link_libraries(lemuri-backend-testing PUBLIC lemuri-backend-parts)
    add_executable(lemuri-core-testing backend/main.cpp)
    target_link_libraries(lemuri-core-testing PRIVATE lemuri-backend-testing)
    foreach(t MonitoringTest SharingTest BackupTest UpdatesTest StoreTest SshTest)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE lemuri-backend Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()

# Fuzzing (clang): the parsers anyone on the network, or any pasted text, can feed. tests/fuzz/README.md.
if(LEMURI_FUZZ)
    foreach(f FuzzLanLine FuzzEnvelope FuzzKnownHosts FuzzSshConfig)
        add_executable(${f} tests/fuzz/${f}.cpp)
        target_link_options(${f} PRIVATE -fsanitize=fuzzer)
        target_link_libraries(${f} PRIVATE lemuri-backend)
    endforeach()
endif()

# cmake --install build --prefix <dir>: the layout of the package (packaging/linux/build.sh stages it this way).
include(GNUInstallDirs)
install(TARGETS lemuri lemuri-core RUNTIME DESTINATION bin)
install(PROGRAMS packaging/linux/lemuri-install DESTINATION bin)
install(FILES backend/package-signers DESTINATION share)
install(FILES LICENSE THIRD-PARTY-NOTICES.md DESTINATION share/doc)
install(DIRECTORY licenses DESTINATION share/doc)
