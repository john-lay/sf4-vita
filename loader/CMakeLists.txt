cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(sf4_vita C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Street Fighter IV CE")
set(VITA_TITLEID "SFIV00001")
set(VITA_VPKNAME "sf4_vita")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/sf4/") # Path to data (with trailing /)
set(SO_PATH "${DATA_PATH}libGltest2jni.so") # The game
set(CXX_SO_PATH "${DATA_PATH}libc++_shared.so") # Its C++ runtime, loaded first (see docs/imports.md)
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}" -DCXX_SO_PATH="${CXX_SO_PATH}")

# Port type
option(NDK_PORT "This should be on if the game is done with Android NDK (has an android_main entry point)" OFF)
if (NDK_PORT)
  add_definitions(-DNDK_PORT)
endif()

# kubridge is a kernel plugin, which Vita3K can't load. KUBRIDGE=OFF builds
# so_util in its VM-domain mode instead so the loader can be tested in Vita3K.
# Real hardware should use the default.
option(KUBRIDGE "Allocate executable memory with kubridge (OFF = VM domain, for Vita3K)" ON)
if (NOT KUBRIDGE)
  add_definitions(-DNO_KUBRIDGE)
endif()

# Vita3K's renderer can't draw ETC1 textures; decode them on the CPU instead.
# Defaults to on for Vita3K builds (KUBRIDGE=OFF); the real GPU handles ETC1.
if (KUBRIDGE)
  option(ETC1_DECODE "Decode ETC1 textures on the CPU (for Vita3K)" OFF)
else()
  option(ETC1_DECODE "Decode ETC1 textures on the CPU (for Vita3K)" ON)
endif()
if (ETC1_DECODE)
  add_definitions(-DETC1_DECODE)
endif()

option(GL_TRACE "Log each distinct GL call/argument combination once (diagnostics)" OFF)
if (GL_TRACE)
  add_definitions(-DGL_TRACE)
endif()

# Test hooks for unattended runs in Vita3K (see loader/README.md).
set(AUTO_TAP "" CACHE STRING "Injected taps: x,y,ms[,x,y,ms...]")
set(AUTO_IME "" CACHE STRING "Answer every text-entry box with this text instead of the IME dialog")
if (AUTO_TAP)
  add_definitions(-DAUTO_TAP=${AUTO_TAP})
endif()
if (AUTO_IME)
  add_definitions(-DAUTO_IME="${AUTO_IME}")
endif()

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

set(SHADER_FORMAT "GLSL") # Preferred shader format (one of 'GLSL', 'CG', 'GXP')
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" OFF)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Unique debug definition for loader's logging to not interfere with anything
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_SOLOADER)
  # Uncomment this for verbose FalsoJNI logging:
  #add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
  # Uncomment this for OpenGL logging:
  #add_definitions(-DDEBUG_OPENGL)
  # Uncomment this for so_util logging:
  #add_definitions(-DSO_UTIL_VERBOSE=1)
  # Uncomment this for verbose FalsoNDK logging (separate things):
  #add_definitions(-DDEBUG_POLL_AND_WAKE=1)
  #add_definitions(-DDEBUG_EPOLL=1)
  #add_definitions(-DDEBUG_PIPEFD=1)
  #add_definitions(-DDEBUG_CALLBACKS=1)
  # Space-separated flags passed to vitaGL
  set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 TEST=1 LOG_ERRORS=1")
else()
  # Space-separated flags passed to vitaGL
  set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 NO_DEBUG=1")
endif()

# vitaGL's boot splash (spinning logo). OFF builds vitaGL with NO_SPLASHSCREEN=1.
option(VITAGL_SPLASH "Show vitaGL's boot splash" ON)
if (NOT VITAGL_SPLASH)
  set(VITAGL_MAKE_FLAGS "${VITAGL_MAKE_FLAGS} NO_SPLASHSCREEN=1")
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif()

# so_util hard-defines USE_KUBRIDGE in its header. For KUBRIDGE=OFF, compile a
# build-time copy with that line removed rather than forking the submodule.
if (KUBRIDGE)
  set(SO_UTIL_SOURCE lib/so_util/so_util.c)
else()
  set(SO_UTIL_COPY ${CMAKE_BINARY_DIR}/so_util_vm/so_util)
  file(READ ${CMAKE_SOURCE_DIR}/lib/so_util/so_util.h so_util_h)
  string(REPLACE "#define USE_KUBRIDGE" "// USE_KUBRIDGE disabled by KUBRIDGE=OFF" so_util_h "${so_util_h}")
  file(WRITE ${SO_UTIL_COPY}/so_util.h "${so_util_h}")
  file(GLOB so_util_files ${CMAKE_SOURCE_DIR}/lib/so_util/*.c ${CMAKE_SOURCE_DIR}/lib/so_util/elf.h)
  file(COPY ${so_util_files} DESTINATION ${SO_UTIL_COPY})
  set(SO_UTIL_SOURCE ${SO_UTIL_COPY}/so_util.c)
endif()

set(SOURCES
    source/main.c
    source/game.c
    source/buttons.c
    source/ime.c
    source/input.c
    source/text.c
    source/gltrace.c
    source/dynlib.c
    source/java.c
    source/patch.c
    source/reimpl/bits/_ctype.c
    source/reimpl/errno.c
    source/reimpl/io.c
    source/reimpl/log.c
    source/reimpl/mem.c
    source/reimpl/pthr.c
    source/reimpl/bigalloc.c
    source/reimpl/sf4_compat.c
    source/reimpl/sl_vita.c
    source/reimpl/sys.c
    source/reimpl/time64.c
    source/utils/dialog.c
    source/utils/glutil.c
    source/utils/init.c
    source/utils/logger.c
    source/utils/settings.c
    source/utils/utils.c
    lib/falso_jni/FalsoJNI.c
    lib/falso_jni/FalsoJNI_ImplBridge.c
    lib/falso_jni/FalsoJNI_Logger.c
    lib/falso_jni/converter.c
    lib/sha1/sha1.c
    lib/fios/fios.c
    ${SO_UTIL_SOURCE})

if(NOT NDK_PORT)
list(APPEND SOURCES
  source/reimpl/asset_manager.cpp
  source/reimpl/controls.c
  source/reimpl/controls.h)
endif()

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

if(NDK_PORT)
  add_subdirectory(lib/falso_ndk)
  add_dependencies(${CMAKE_PROJECT_NAME} FalsoNDK)
endif()

# VitaGL built from lib/vitagl via its own Makefile.
# Made this way so that testing quick adjustments to VitaGL is possible,
# as well as changing build flags in a fast and efficient way.
separate_arguments(VITAGL_MAKE_ARGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS}")

set(VITAGL_DIR ${CMAKE_SOURCE_DIR}/lib/vitagl)
set(VITAGL_LIB ${VITAGL_DIR}/libvitaGL.a)

# Force a full rebuild of vitaGL whenever VITAGL_MAKE_FLAGS change
set(VITAGL_FLAGS_STAMP ${CMAKE_BINARY_DIR}/vitagl_flags.stamp)
set(vitagl_old_flags "")
if (EXISTS ${VITAGL_FLAGS_STAMP})
  file(READ ${VITAGL_FLAGS_STAMP} vitagl_old_flags)
endif()
if (NOT vitagl_old_flags STREQUAL VITAGL_MAKE_FLAGS)
  file(GLOB_RECURSE vitagl_stale_objs ${VITAGL_DIR}/source/*.o)
  file(REMOVE ${vitagl_stale_objs} ${VITAGL_LIB})
  file(WRITE ${VITAGL_FLAGS_STAMP} "${VITAGL_MAKE_FLAGS}")
endif()

include(ProcessorCount)
ProcessorCount(VITAGL_NPROC)
if (VITAGL_NPROC EQUAL 0)
  set(VITAGL_NPROC 4)
endif()

add_custom_target(vitaGL_build
                  COMMAND make -C ${VITAGL_DIR} -j${VITAGL_NPROC} ${VITAGL_MAKE_ARGS}
                  BYPRODUCTS ${VITAGL_LIB}
                  COMMENT "Building VitaGL"
                  VERBATIM)

# Which vitaGL to link. The template builds lib/vitagl (pinned at 9c23758), but
# that build renders nothing at all in Vita3K (checked 2026-10-15 with a bare
# red-quad probe), while VitaSDK's softfp vitaGL package works. Untested on
# hardware. OFF goes back to the template's submodule build.
option(SDK_VITAGL "Use the VitaSDK vitaGL package instead of building lib/vitagl" ON)
add_library(vitaGL STATIC IMPORTED)
if (SDK_VITAGL)
  set_target_properties(vitaGL PROPERTIES
                        IMPORTED_LOCATION $ENV{VITASDK}/arm-vita-eabi/lib/libvitaGL.a
                        INTERFACE_INCLUDE_DIRECTORIES $ENV{VITASDK}/arm-vita-eabi/include)
else()
  set_target_properties(vitaGL PROPERTIES
                        IMPORTED_LOCATION ${VITAGL_LIB}
                        INTERFACE_INCLUDE_DIRECTORIES ${VITAGL_DIR}/source)
  add_dependencies(${CMAKE_PROJECT_NAME} vitaGL_build)
endif()

if (NOT KUBRIDGE)
  # Must come before lib/ so <so_util/so_util.h> picks up the VM-mode copy.
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM BEFORE
                             PUBLIC ${CMAKE_BINARY_DIR}/so_util_vm)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

if(NDK_PORT)
  target_link_libraries(${CMAKE_PROJECT_NAME}
                        FalsoNDK
                        )
endif()

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      OpenSLES
                      sndfile
                      mp3lame
                      mpg123
                      opus
                      vorbis
                      ogg
                      vorbisenc
                      vorbisfile
                      FLAC
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      png
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      ScePvf_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE ${CMAKE_SOURCE_DIR}/extras/cpuinfo cpuinfo
                ${CMAKE_SOURCE_DIR}/extras/meminfo meminfo
                ${CMAKE_SOURCE_DIR}/extras/urandom urandom
                ${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png sce_sys/icon0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png sce_sys/pic0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/startup.png sce_sys/livearea/contents/startup.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png sce_sys/livearea/contents/bg0.png
                ${CMAKE_SOURCE_DIR}/extras/livearea/template.xml sce_sys/livearea/contents/template.xml
                )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
