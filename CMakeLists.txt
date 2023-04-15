cmake_minimum_required(VERSION 3.15)
project(scintilla VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lexlib
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

IF(CMAKE_BUILD_TYPE MATCHES Debug)
    set(BUILD_TYPE Debug)
    add_compile_definitions(LOGS_ENABLED)
ELSE(CMAKE_BUILD_TYPE MATCHES Debug)
    set(BUILD_TYPE Release)
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)

option(NOAVX2 "Target CPU does NOT support AVX2")

IF(NOT NOAVX2 AND NOT CMAKE_BUILD_TYPE MATCHES Debug)
    IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        IF(MSVC)
            IF(${CMAKE_CXX_COMPILER_ID} STREQUAL "IntelLLVM")
                add_compile_options("/QxCORE-AVX2" "-O3" "-mavx2")
            ELSE()
                add_compile_options("/O2" "/arch:AVX2")
            ENDIF()
        ELSEIF (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options("-O3" "-mavx2")
        ELSEIF (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options("-O3" "-mavx2")
        ENDIF(MSVC)
    ENDIF()
ENDIF()

set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

add_compile_definitions(
        SCI_LEXER=1
        _CRT_SECURE_NO_DEPRECATE=1
        STRSAFE_NO_DEPRECATE
        SCI_STATIC_LINK=1
        NO_DLL=1
        EXPORT_IMPORT_API=
        NO_CXX11_REGEX
        )

file(GLOB
        scintilla_src
        ./src/*.cxx
        ./lexers/*.cxx
        ./lexlib/*.cxx
)

if(WIN32)
    add_compile_definitions(_WINDOWS 
        NOMINMAX 
        WIN32_LEAN_AND_MEAN 
        STRICT_TYPED_ITEMIDS 
        UNICODE 
        _UNICODE
        )
    file(GLOB
            win32_src
            ./win32/*.cxx
            ./win32/*.c
    )
endif(WIN32)

add_library(${PROJECT_NAME} STATIC ${scintilla_src} ${win32_src})

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        kernel32 user32 gdi32 comctl32 comdlg32 advapi32 shlwapi
        shell32 ole32 oleaut32 uuid uxtheme imm32
        )
endif(WIN32)

target_include_directories (${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR} 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lexlib
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
