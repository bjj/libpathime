# The find_package() half of the install check — see consumer.c for what the
# program asserts and why. Point CMAKE_PREFIX_PATH at an installed libpathime.
# The pkg-config half needs no CMake: CI compiles consumer.c directly against
# `pkg-config --cflags --libs [--static] pathime`.
cmake_minimum_required(VERSION 3.21)
project(pathime-install-check C)

# A C project, as a consumer of a C API should be — but a *static* libpathime
# is C++ behind the C header, and CMake only links the C++ runtime for targets
# whose languages need it. The imported target says so through
# IMPORTED_LINK_INTERFACE_LANGUAGES; enabling the language here is the whole
# accommodation, and it is a no-op against a shared install.
enable_language(CXX)

find_package(pathime REQUIRED)

add_executable(pathime-install-check consumer.c)
target_link_libraries(pathime-install-check PRIVATE libpathime::pathime)
