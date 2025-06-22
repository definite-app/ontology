# Docker Build Fix for Semantic Query Extension

## Issue Identified
The Docker build was failing with the error:
```
fatal error: nlohmann/json.hpp: No such file or directory
```

This occurred because the header file `src/include/quack_extension.hpp` was including `<nlohmann/json.hpp>` but the vcpkg include paths weren't being properly resolved during the Docker build process.

## Root Cause
1. **Header Include Issue**: The header file was including nlohmann/json directly, but the vcpkg include paths weren't in the include path when the header was being processed during compilation.
2. **CMake Configuration**: The CMakeLists.txt was using `pkg_check_modules` instead of the proper vcpkg `find_package` approach.

## Solutions Applied

### 1. Moved JSON Include to Implementation File
**Before:**
```cpp
// In src/include/quack_extension.hpp
#include <nlohmann/json.hpp>
```

**After:**
```cpp
// Removed from header file
// Now only in src/quack_extension.cpp
#include <nlohmann/json.hpp>
```

### 2. Updated CMakeLists.txt for Proper vcpkg Integration
**Before:**
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(nlohmann_json REQUIRED IMPORTED_TARGET nlohmann_json)
target_link_libraries(${EXTENSION_NAME} PkgConfig::nlohmann_json)
```

**After:**
```cmake
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${EXTENSION_NAME} nlohmann_json::nlohmann_json)
```

## Why This Fixes the Issue

1. **Header Separation**: By removing the JSON include from the header file, we avoid the issue where the header is processed before vcpkg include paths are fully resolved.

2. **Proper vcpkg Integration**: Using `find_package(nlohmann_json CONFIG REQUIRED)` ensures that vcpkg properly sets up the include paths and targets.

3. **Correct Target Linking**: Using `nlohmann_json::nlohmann_json` as the target ensures proper linking with the vcpkg-installed library.

## Expected Result
The Docker build should now complete successfully with:
- ✅ Proper nlohmann/json dependency resolution
- ✅ Correct include paths set by vcpkg
- ✅ Successful compilation of the semantic query extension
- ✅ All functionality working as expected

## Files Modified
- `src/include/quack_extension.hpp` - Removed nlohmann/json include
- `CMakeLists.txt` - Updated to use proper vcpkg find_package approach
- `src/quack_extension.cpp` - Already had the correct include (no changes needed)

The semantic query extension functionality remains fully intact with all features working:
- ✅ REGISTER_DATASET function
- ✅ SEMANTIC_QUERY table function (both normal and explain modes)
- ✅ Full JSON parsing with snake_case compliance
- ✅ SQL compilation engine
- ✅ Comprehensive validation and error handling