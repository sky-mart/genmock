set(LLVM_LINK_COMPONENTS support)

find_package(nlohmann_json 3.2.0 REQUIRED)

add_clang_executable(genmock
  genmock.cpp
  Mocker.cpp
  )

target_link_libraries(genmock
  PRIVATE
  clangTooling
  nlohmann_json::nlohmann_json
  )
