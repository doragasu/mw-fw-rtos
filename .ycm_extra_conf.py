# This file is NOT licensed under the GPLv3, which is the license for the rest
# of YouCompleteMe.
#
# Here's the license text for this file:
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# For more information, please refer to <http://unlicense.org/>

import os
import ycm_core

# These are the compilation flags that will be used in case there's no
# compilation database set (by default, one is not set).
# CHANGE THIS LIST OF FLAGS. YES, THIS IS THE DROID YOU HAVE BEEN LOOKING FOR.
idf_path = os.environ['IDF_PATH']
prj_path = os.environ['PWD']
flags = [
    '-x',
    'c',
    '-std=gnu99',
    '-fstrict-volatile-bitfields',
    '-Wall',
    '-Wno-error=unused-function',
    '-Wno-error=unused-but-set-variable',
    '-Wno-error=unused-variable',
    '-Wno-error=deprecated-declarations',
    '-Wextra',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-old-style-declaration',
    '-DCONFIG_ENABLE_BOOT_CHECK_SUM=1',
    '-DICACHE_FLASH',
    '-D__ESP_FILE__=app_main.c',
    '-D_CLOCKS_PER_SEC_=CONFIG_FREERTOS_HZ',
    '-D_POSIX_THREADS=1',
    '-D_UNIX98_THREAD_MUTEX_ATTRIBUTES=1',
    '-DAPP_OFFSET=0x10000',
    '-DAPP_SIZE=0xf0000',
    '-DESP_PLATFORM',
    '-D',
    'IDF_VER="v3.2-9-g6d9e8706"',
    '-DGCC_NOT_5_2_0=0',
    '-DWITH_POSIX',
    '-DMQTT_TASK',
    '-DMQTTCLIENT_PLATFORM_HEADER=MQTTFreeRTOS.h',
    '-D_CLOCKS_PER_SEC_=CONFIG_FREERTOS_HZ',
    '-D_POSIX_THREADS=1',
    '-D_UNIX98_THREAD_MUTEX_ATTRIBUTES=1',
    '-DMBEDTLS_CONFIG_FILE=mbedtls/esp_config.h',
    '-DWOLFSSL_USER_SETTINGS',
    '-I',
    prj_path + '/main/include',
    '-I',
    idf_path + '/components/app_update/include',
    '-I',
    idf_path + '/components/bootloader_support/include',
    '-I',
    idf_path + '/components/cjson/include',
    '-I',
    idf_path + '/components/cjson/cJSON',
    '-I',
    idf_path + '/components/coap/port/include',
    '-I',
    idf_path + '/components/coap/port/include/coap',
    '-I',
    idf_path + '/components/coap/libcoap/include',
    '-I',
    idf_path + '/components/coap/libcoap/include/coap',
    '-I',
    idf_path + '/components/esp-tls',
    '-I',
    idf_path + '/components/esp8266/include',
    '-I',
    idf_path + '/components/esp8266/include',
    '-I',
    idf_path + '/components/esp_http_client/include',
    '-I',
    idf_path + '/components/esp_http_server/include',
    '-I',
    idf_path + '/components/esp_https_ota/include',
    '-I',
    idf_path + '/components/esp_ringbuf/include',
    '-I',
    idf_path + '/components/esp_ringbuf/include/freertos',
    '-I',
    idf_path + '/components/freertos/include',
    '-I',
    idf_path + '/components/freertos/include',
    '-I',
    idf_path + '/components/freertos/include/freertos',
    '-I',
    idf_path + '/components/freertos/include/freertos/private',
    '-I',
    idf_path + '/components/freertos/port/esp8266/include',
    '-I',
    idf_path + '/components/freertos/port/esp8266/include/freertos',
    '-I',
    idf_path + '/components/heap/include',
    '-I',
    idf_path + '/components/heap/port/esp8266/include',
    '-I',
    idf_path + '/components/http_parser/include',
    '-I',
    idf_path + '/components/jsmn/include',
    '-I',
    idf_path + '/components/libsodium/libsodium/src/libsodium/include',
    '-I',
    idf_path + '/components/libsodium/port_include',
    '-I',
    idf_path + '/components/log/include',
    '-I',
    idf_path + '/components/lwip/include',
    '-I',
    idf_path + '/components/lwip/include/lwip/apps',
    '-I',
    idf_path + '/components/lwip/include/lwip',
    '-I',
    idf_path + '/components/lwip/lwip/src/include',
    '-I',
    idf_path + '/components/lwip/lwip/src/include/posix',
    '-I',
    idf_path + '/components/lwip/port/esp8266/include',
    '-I',
    idf_path + '/components/lwip/port/esp8266/include/port',
    '-I',
    idf_path + '/components/mdns/include',
    '-I',
    idf_path + '/components/mqtt/include',
    '-I',
    idf_path + '/components/mqtt/ibm-mqtt/MQTTClient-C/src',
    '-I',
    idf_path + '/components/mqtt/ibm-mqtt/MQTTClient-C/src/FreeRTOS',
    '-I',
    idf_path + '/components/mqtt/ibm-mqtt/MQTTPacket/src',
    '-I',
    idf_path + '/components/newlib/include',
    '-I',
    idf_path + '/components/newlib/newlib/include',
    '-I',
    idf_path + '/components/newlib/newlib/port/include',
    '-I',
    idf_path + '/components/nvs_flash/include',
    '-I',
    idf_path + '/components/protobuf-c/protobuf-c',
    '-I',
    idf_path + '/components/pthread/include',
    '-I',
    idf_path + '/components/smartconfig_ack/include',
    '-I',
    idf_path + '/components/spi_flash/include',
    '-I',
    idf_path + '/components/spi_ram/include',
    '-I',
    idf_path + '/components/ssl/include',
    '-I',
    idf_path + '/components/ssl/mbedtls/mbedtls/include',
    '-I',
    idf_path + '/components/ssl/mbedtls/port/esp8266/include',
    '-I',
    idf_path + '/components/ssl/mbedtls/port/openssl/include',
    '-I',
    idf_path + '/components/tcp_transport/include',
    '-I',
    idf_path + '/components/tcpip_adapter/include',
    '-I',
    idf_path + '/components/tcpip_adapter/include',
    '-I',
    idf_path + '/components/util/include',
    '-I',
    idf_path + '/components/vfs/include',
    '-I',
    idf_path + '/components/wpa_supplicant/include',
    '-I',
    idf_path + '/components/wpa_supplicant/port/include',
    '-I',
    prj_path + '/build/include',
    '-I',
    '.',
]


# Set this to the absolute path to the folder (NOT the file!) containing the
# compile_commands.json file to use that instead of 'flags'. See here for
# more details: http://clang.llvm.org/docs/JSONCompilationDatabase.html
#
# You can get CMake to generate this file for you by adding:
#   set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )
# to your CMakeLists.txt file.
#
# Most projects will NOT need to set this to anything; you can just change the
# 'flags' list of compilation flags. Notice that YCM itself uses that approach.
compilation_database_folder = ''

if os.path.exists( compilation_database_folder ):
  database = ycm_core.CompilationDatabase( compilation_database_folder )
else:
  database = None

SOURCE_EXTENSIONS = [ '.cpp', '.cxx', '.cc', '.c', '.m', '.mm' ]

def DirectoryOfThisScript():
  return os.path.dirname( os.path.abspath( __file__ ) )


def MakeRelativePathsInFlagsAbsolute( flags, working_directory ):
  if not working_directory:
    return list( flags )
  new_flags = []
  make_next_absolute = False
  path_flags = [ '-isystem', '-I', '-iquote', '--sysroot=' ]
  for flag in flags:
    new_flag = flag

    if make_next_absolute:
      make_next_absolute = False
      if not flag.startswith( '/' ):
        new_flag = os.path.join( working_directory, flag )

    for path_flag in path_flags:
      if flag == path_flag:
        make_next_absolute = True
        break

      if flag.startswith( path_flag ):
        path = flag[ len( path_flag ): ]
        new_flag = path_flag + os.path.join( working_directory, path )
        break

    if new_flag:
      new_flags.append( new_flag )
  return new_flags


def IsHeaderFile( filename ):
  extension = os.path.splitext( filename )[ 1 ]
  return extension in [ '.h', '.hxx', '.hpp', '.hh' ]


def GetCompilationInfoForFile( filename ):
  # The compilation_commands.json file generated by CMake does not have entries
  # for header files. So we do our best by asking the db for flags for a
  # corresponding source file, if any. If one exists, the flags for that file
  # should be good enough.
  if IsHeaderFile( filename ):
    basename = os.path.splitext( filename )[ 0 ]
    for extension in SOURCE_EXTENSIONS:
      replacement_file = basename + extension
      if os.path.exists( replacement_file ):
        compilation_info = database.GetCompilationInfoForFile(
          replacement_file )
        if compilation_info.compiler_flags_:
          return compilation_info
    return None
  return database.GetCompilationInfoForFile( filename )


def FlagsForFile( filename, **kwargs ):
  if database:
    # Bear in mind that compilation_info.compiler_flags_ does NOT return a
    # python list, but a "list-like" StringVec object
    compilation_info = GetCompilationInfoForFile( filename )
    if not compilation_info:
      return None

    final_flags = MakeRelativePathsInFlagsAbsolute(
      compilation_info.compiler_flags_,
      compilation_info.compiler_working_dir_ )

    # NOTE: This is just for YouCompleteMe; it's highly likely that your project
    # does NOT need to remove the stdlib flag. DO NOT USE THIS IN YOUR
    # ycm_extra_conf IF YOU'RE NOT 100% SURE YOU NEED IT.
    try:
      final_flags.remove( '-stdlib=libc++' )
    except ValueError:
      pass
  else:
    relative_to = DirectoryOfThisScript()
    final_flags = MakeRelativePathsInFlagsAbsolute( flags, relative_to )

  return {
    'flags': final_flags,
    'do_cache': True
  }
