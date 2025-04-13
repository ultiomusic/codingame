#include <iostream>

int main() {
    std::cout << "C++ Standard (__cplusplus): " << __cplusplus << ", ";

    #ifdef __clang__
        std::cout << "Derleyici: Clang, Versiyon: " << __clang_major__ << "." 
                  << __clang_minor__ << "." << __clang_patchlevel__ << ", ";
    #elif defined(__GNUC__)
        std::cout << "Derleyici: GCC, Versiyon: " << __GNUC__ << "." 
                  << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << ", ";
    #elif defined(_MSC_VER)
        std::cout << "Derleyici: Microsoft Visual C++, Versiyon: " << _MSC_VER << ", ";
    #else
        std::cout << "Derleyici: Bilinmiyor, ";
    #endif

    #if defined(_WIN32) || defined(_WIN64)
        std::cout << "İşletim Sistemi: Windows, ";
    #elif defined(__linux__)
        std::cout << "İşletim Sistemi: Linux, ";
    #elif defined(__APPLE__) && defined(__MACH__)
        std::cout << "İşletim Sistemi: macOS, ";
    #else
        std::cout << "İşletim Sistemi: Bilinmiyor, ";
    #endif

    #if defined(__x86_64__) || defined(_M_X64)
        std::cout << "Mimari: x86_64 (64-bit)";
    #elif defined(__i386) || defined(_M_IX86)
        std::cout << "Mimari: x86 (32-bit)";
    #elif defined(__aarch64__)
        std::cout << "Mimari: ARM64";
    #elif defined(__arm__) || defined(_M_ARM)
        std::cout << "Mimari: ARM";
    #else
        std::cout << "Mimari: Bilinmiyor";
    #endif

    std::cout << std::endl;
    return 0;
}
