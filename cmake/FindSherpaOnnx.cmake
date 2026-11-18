# 查找 sherpa-onnx 的 C API 头文件与共享库。
#
# 预编译包没有 CMake config，因此通过 SHERPA_ONNX_ROOT、CMAKE_PREFIX_PATH 或
# /opt/sherpa-onnx、/usr/local 查找。可显式传入：
#   -DSHERPA_ONNX_ROOT=/opt/sherpa-onnx

# 缓存的查找结果指向已被删除/移动的目录时作废，重新查找。
# 典型场景：/tmp 下的临时解压目录被清理，而 CMakeCache 仍记录旧路径。
foreach (_sherpa_cached_var SherpaOnnx_INCLUDE_DIR SherpaOnnx_C_API_LIBRARY SherpaOnnx_ONNXRUNTIME_LIBRARY)
    if (DEFINED ${_sherpa_cached_var} AND NOT EXISTS "${${_sherpa_cached_var}}")
        unset(${_sherpa_cached_var} CACHE)
    endif ()
endforeach ()
unset(_sherpa_cached_var)

find_path(SherpaOnnx_INCLUDE_DIR
        NAMES sherpa-onnx/c-api/c-api.h
        HINTS ${SHERPA_ONNX_ROOT} ENV SHERPA_ONNX_ROOT
        PATHS /opt/sherpa-onnx /usr/local /usr
        PATH_SUFFIXES include
)

find_library(SherpaOnnx_C_API_LIBRARY
        NAMES sherpa-onnx-c-api
        HINTS ${SHERPA_ONNX_ROOT} ENV SHERPA_ONNX_ROOT
        PATHS /opt/sherpa-onnx /usr/local /usr
        PATH_SUFFIXES lib lib64
)

find_library(SherpaOnnx_ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS ${SHERPA_ONNX_ROOT} ENV SHERPA_ONNX_ROOT
        PATHS /opt/sherpa-onnx /usr/local /usr
        PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SherpaOnnx
        REQUIRED_VARS SherpaOnnx_INCLUDE_DIR SherpaOnnx_C_API_LIBRARY
)

if (SherpaOnnx_FOUND AND NOT TARGET SherpaOnnx::c-api)
    add_library(SherpaOnnx::c-api UNKNOWN IMPORTED)
    set_target_properties(SherpaOnnx::c-api PROPERTIES
            IMPORTED_LOCATION "${SherpaOnnx_C_API_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SherpaOnnx_INCLUDE_DIR}"
    )
    if (SherpaOnnx_ONNXRUNTIME_LIBRARY)
        set_property(TARGET SherpaOnnx::c-api PROPERTY
                INTERFACE_LINK_LIBRARIES "${SherpaOnnx_ONNXRUNTIME_LIBRARY}")
    endif ()
endif ()

mark_as_advanced(SherpaOnnx_INCLUDE_DIR SherpaOnnx_C_API_LIBRARY SherpaOnnx_ONNXRUNTIME_LIBRARY)
