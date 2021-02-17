# Set up Hunter
set(HUNTER_URL "https://github.com/LLVMParty/hunter/archive/51571e846b8dc24997bd43e6459f67babceea77d.zip")
set(HUNTER_SHA1 "53679D956A97DD135723A43C79C363E5312B5490")

set(HUNTER_LLVM_VERSION 11.0.1)
set(HUNTER_LLVM_CMAKE_ARGS
    LLVM_ENABLE_CRASH_OVERRIDES=OFF
    LLVM_ENABLE_ASSERTIONS=ON
    LLVM_ENABLE_PROJECTS=clang;lld
)
set(HUNTER_PACKAGES LLVM)

include(FetchContent)
message(STATUS "Fetching hunter...")
FetchContent_Declare(SetupHunter GIT_REPOSITORY https://github.com/cpp-pm/gate)
FetchContent_MakeAvailable(SetupHunter)