# Alternative to blogy.pro for people who prefer CMake. scripts/build.sh uses qmake,
# like Cinder. Both produce build/blogy.
cmake_minimum_required(VERSION 3.16)
project(blogy VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

file(GLOB_RECURSE BLOGY_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(blogy ${BLOGY_SOURCES} resources.qrc)
target_include_directories(blogy PRIVATE src)
target_compile_options(blogy PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(blogy PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# dist/bin/blogy finds dist/lib first, then the Qt it was built with.
set_target_properties(blogy PROPERTIES
    BUILD_RPATH "\$ORIGIN/../lib;${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}"
    INSTALL_RPATH "\$ORIGIN/../lib")
target_link_options(blogy PRIVATE -Wl,--disable-new-dtags)

install(TARGETS blogy RUNTIME DESTINATION bin)
install(FILES share/blogy.desktop DESTINATION share/applications)
install(FILES resources/blogy.svg DESTINATION share/icons/hicolor/scalable/apps)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES resources/icons/blogy-${size}.png DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME blogy.png)
endforeach()
