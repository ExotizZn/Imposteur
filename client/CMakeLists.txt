cmake_minimum_required (VERSION 3.11)
 
# --- Fetch FTXUI --------------------------------------------------------------
include(FetchContent)
 
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG a8eda59d9894e31c02c00812e80bd3b79f2ccdfe
)
 
FetchContent_GetProperties(ftxui)
if(NOT ftxui_POPULATED)
  FetchContent_Populate(ftxui)
  add_subdirectory(${ftxui_SOURCE_DIR} ${ftxui_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
 
# ------------------------------------------------------------------------------
 
project(imposteur_client
  LANGUAGES CXX
  VERSION 1.0.0
)
 
add_executable(imposteur_client src/main.cpp)
target_include_directories(imposteur_client PRIVATE src)
 
target_link_libraries(imposteur_client
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component # Not needed for this example.
)