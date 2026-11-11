# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/joaopedro/Documentos/ufc/semestre_07/RTOS/esp-idf/components/bootloader/subproject"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/tmp"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/src/bootloader-stamp"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/src"
  "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/joaopedro/Documentos/ufc/semestre_08/instrumetacao/projeto/ir/esp_idf/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
