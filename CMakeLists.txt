cmake_minimum_required(VERSION 3.24)
project(lab08 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "in-source builds overwrite this project's Makefile. Use: cmake -S . -B build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

find_path(GLM_INCLUDE_DIR glm/glm.hpp
  HINTS $ENV{VULKAN_SDK}/include ${Vulkan_INCLUDE_DIR}
  PATHS /usr/local/include /opt/homebrew/include /usr/include
)

if(NOT GLM_INCLUDE_DIR)
  message(FATAL_ERROR "glm/glm.hpp not found. It comes with the Vulkan SDK: set VULKAN_SDK "
                      "or source the SDK's setup-env.sh. On Linux with apt, install libglm-dev")
endif()

add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE ${GLM_INCLUDE_DIR})

set(STB_COMMIT f0569113c93ad095470c54bf34a17b36646bbbb5)
set(STB_DIR ${CMAKE_SOURCE_DIR}/external/stb)

foreach(HEADER stb_image.h stb_image_write.h)
  if(NOT EXISTS ${STB_DIR}/${HEADER})
    message(STATUS "fetching ${HEADER}")
    file(DOWNLOAD
      https://raw.githubusercontent.com/nothings/stb/${STB_COMMIT}/${HEADER}
      ${STB_DIR}/${HEADER}
      STATUS STB_STATUS
      TLS_VERIFY ON
    )
    list(GET STB_STATUS 0 STB_CODE)
    if(NOT STB_CODE EQUAL 0)
      file(REMOVE ${STB_DIR}/${HEADER})
      message(FATAL_ERROR "could not download ${HEADER}. Run external/fetch.sh, "
                          "or copy the two stb headers into external/stb by hand")
    endif()
  endif()
endforeach()

file(SHA256 ${STB_DIR}/stb_image.h STB_IMAGE_HASH)
if(NOT STB_IMAGE_HASH STREQUAL
   "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3")
  message(FATAL_ERROR "external/stb/stb_image.h does not match the pinned commit")
endif()

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${STB_DIR})

set(STUDENT_SHADERS "" CACHE STRING "directory whose shaders override shaders/")

set(SHADER_DIRS "${CMAKE_SOURCE_DIR}/shaders")
if(STUDENT_SHADERS)
  get_filename_component(STUDENT_DIR "${STUDENT_SHADERS}" ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
  if(NOT IS_DIRECTORY "${STUDENT_DIR}")
    message(FATAL_ERROR "STUDENT_SHADERS is not a directory: ${STUDENT_DIR}")
  endif()
  set(SHADER_DIRS "${STUDENT_DIR}" "${CMAKE_SOURCE_DIR}/shaders")
  message(STATUS "student shaders: ${STUDENT_DIR}")
endif()

add_library(lab08core STATIC
  src/core/Device.cpp
  src/core/Buffer.cpp
  src/core/Target.cpp
  src/core/Texture.cpp
  src/core/Sampler.cpp
  src/core/Pass.cpp
  src/core/Pipeline.cpp
  src/core/Geometry.cpp
  src/core/Shader.cpp
  src/core/Frame.cpp
  src/core/Timer.cpp
  src/core/Window.cpp
  src/utils/Args.cpp
  src/utils/utils.cpp
  src/utils/image.cpp
  src/utils/Mesh.cpp
)

target_include_directories(lab08core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(lab08core PUBLIC
  Vulkan::Vulkan
  glfw
  glm
  stb
)

target_compile_options(lab08core PUBLIC -Wall -Wextra)

find_program(GLSLC NAMES glslc glslangValidator)

if(NOT GLSLC)
  message(FATAL_ERROR "no GLSL compiler found. glslc comes with the Vulkan SDK; "
                      "on Linux, apt install glslang-tools")
endif()

string(JOIN ";" SHADER_SRC_DIRS ${SHADER_DIRS})
string(REPLACE ";" "\;" SHADER_SRC_DIRS "${SHADER_SRC_DIRS}")

target_compile_definitions(lab08core PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_FORCE_RADIANS
  SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
  GLSLC="${GLSLC}"
  REFERENCE_DIR="${CMAKE_SOURCE_DIR}/reference"
  REPORT_DIR="${CMAKE_SOURCE_DIR}/report"
  "SHADER_SRC_DIRS=\"${SHADER_SRC_DIRS}\""
)

set(PARTS info srgb normalmap floor oit)

foreach(PART ${PARTS})
  add_executable(${PART} src/${PART}.cpp)
  target_link_libraries(${PART} PRIVATE lab08core)
endforeach()

set(GLSL_INCLUDES "")
foreach(DIR ${SHADER_DIRS})
  file(GLOB DIR_INCLUDES "${DIR}/*.glsl")
  list(APPEND GLSL_INCLUDES ${DIR_INCLUDES})
  list(APPEND GLSLC_INCLUDE_FLAGS "-I${DIR}")
endforeach()

file(GLOB GLSL_FILES
  "${CMAKE_SOURCE_DIR}/shaders/*.vert"
  "${CMAKE_SOURCE_DIR}/shaders/*.frag"
)

set(SPIRV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

if(GLSLC MATCHES "glslangValidator")
  set(GLSLC_ARGS -V)
endif()

foreach(GLSL ${GLSL_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SOURCE ${GLSL})
  if(STUDENT_DIR AND EXISTS "${STUDENT_DIR}/${FILE_NAME}")
    set(SOURCE "${STUDENT_DIR}/${FILE_NAME}")
  endif()
  set(SPIRV "${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSLC} ${GLSLC_ARGS} ${GLSLC_INCLUDE_FLAGS} ${SOURCE} -o ${SPIRV}
    DEPENDS ${SOURCE} ${GLSL_INCLUDES}
  )
  list(APPEND SPIRV_FILES ${SPIRV})
endforeach()

add_custom_target(shaders DEPENDS ${SPIRV_FILES})
add_dependencies(lab08core shaders)
