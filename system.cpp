#include <iostream>

int main() {
    std::cout << "C++ Standard (__cplusplus): " << __cplusplus << ", ";

    #ifdef __clang__
        std::cout << "Compiler: Clang, Version: " << __clang_major__ << "." 
                  << __clang_minor__ << "." << __clang_patchlevel__ << ", ";
    #elif defined(__GNUC__)
        std::cout << "Compiler: GCC, Version: " << __GNUC__ << "." 
                  << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << ", ";
    #elif defined(_MSC_VER)
        std::cout << "Compiler: Microsoft Visual C++, Version: " << _MSC_VER << ", ";
    #else
        std::cout << "Compiler: Unknown, ";
    #endif

    #if defined(_WIN32) || defined(_WIN64)
        std::cout << "Operating System: Windows, ";
    #elif defined(__linux__)
        std::cout << "Operating System: Linux, ";
    #elif defined(__APPLE__) && defined(__MACH__)
        std::cout << "Operating System: macOS, ";
    #else
        std::cout << "Operating System: Unknown, ";
    #endif

    #if defined(__x86_64__) || defined(_M_X64)
        std::cout << "Architecture: x86_64 (64-bit)";
    #elif defined(__i386) || defined(_M_IX86)
        std::cout << "Architecture: x86 (32-bit)";
    #elif defined(__aarch64__)
        std::cout << "Architecture: ARM64";
    #elif defined(__arm__) || defined(_M_ARM)
        std::cout << "Architecture: ARM";
    #else
        std::cout << "Architecture: Unknown";
    #endif

    std::cout << std::endl;
    return 0;
}
