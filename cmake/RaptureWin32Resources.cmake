# rapture_add_win32_resources(<target>
#     [ICON_FROM <pe-file>] [ICON_GROUP <id>]
#     VERSION <a.b.c[.d]>  PRODUCT <name>  DESCRIPTION <text>  FILENAME <name.exe>
#     [INTERNAL_NAME <name>] [COMPANY <text>] [COPYRIGHT <text>] [COMMENTS <text>]
#     [DPI UNAWARE|SYSTEM|PERMONITOR])
#
# Gives a Windows executable proper resources, generated at CONFIGURE time into <build>/rapture_rc/<target>/ (nothing is stored in a repository):
#   * resource 1 = icon, extracted from the multi-image icon group of <pe-file> (a file the USER owns: the original game's exe) with
#     tools/pe_resources.py (standard library Python).  Explorer shows it, and SDL2 uses resource 1 as the icon of its window class,
#     so the title bar / taskbar match.  If <pe-file> is missing or has no icon the target is built WITHOUT icon (warning, never an error).
#   * a VERSIONINFO block (CompanyName, FileDescription, FileVersion, InternalName, LegalCopyright, OriginalFilename, ProductName,
#     ProductVersion, Comments).  Defaults for COMPANY/COPYRIGHT/COMMENTS say that this is an unofficial fan port.
#   * an application manifest: requestedExecutionLevel asInvoker, supportedOS Windows Vista..11, DPI awareness as given by DPI
#     (default UNAWARE = no dpiAware element, which is how SDL2 programs without a manifest behave: Windows scales the window itself).
# Compiled with the RC language of CMake (MinGW windres).  No-op on non-Windows or when the RC language is unavailable.
# Plain ASCII only in the strings (the .rc is compiled as is).

set(_RAPTURE_RES_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "directory of RaptureWin32Resources.cmake")

function(_rapture_rc_escape out in)
    string(REPLACE "\\" "\\\\" s "${in}")
    string(REPLACE "\"" "\"\"" s "${s}")
    set(${out} "${s}" PARENT_SCOPE)
endfunction()

function(_rapture_write_if_changed path content)
    set(old "")
    if(EXISTS "${path}")
        file(READ "${path}" old)
    endif()
    if(NOT old STREQUAL content)
        file(WRITE "${path}" "${content}")
    endif()
endfunction()

function(rapture_add_win32_resources target)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(R "" "ICON_FROM;ICON_GROUP;VERSION;PRODUCT;DESCRIPTION;FILENAME;INTERNAL_NAME;COMPANY;COPYRIGHT;COMMENTS;DPI" "" ${ARGN})
    if(NOT R_VERSION)
        set(R_VERSION "1.0.0")
    endif()
    if(NOT R_PRODUCT)
        set(R_PRODUCT "${target}")
    endif()
    if(NOT R_DESCRIPTION)
        set(R_DESCRIPTION "${R_PRODUCT}")
    endif()
    if(NOT R_FILENAME)
        set(R_FILENAME "${target}.exe")
    endif()
    if(NOT R_INTERNAL_NAME)
        set(R_INTERNAL_NAME "${target}")
    endif()
    if(NOT R_COMPANY)
        set(R_COMPANY "Unofficial fan port (not affiliated with the original publisher)")
    endif()
    if(NOT R_COPYRIGHT)
        set(R_COPYRIGHT "Unofficial fan port for personal use; the original game belongs to its copyright holders")
    endif()
    if(NOT R_COMMENTS)
        set(R_COMMENTS "Unofficial fan port; needs the user's own copy of the original game")
    endif()
    if(NOT R_DPI)
        set(R_DPI "UNAWARE")
    endif()

    if(NOT CMAKE_RC_COMPILER_LOADED)
        enable_language(RC OPTIONAL)
    endif()
    if(NOT CMAKE_RC_COMPILER)
        message(WARNING "rapture_add_win32_resources: no resource compiler (windres) found; ${target} is built without icon/version/manifest")
        return()
    endif()

    set(dir "${CMAKE_CURRENT_BINARY_DIR}/rapture_rc/${target}")
    file(MAKE_DIRECTORY "${dir}")

    # ---- icon ----
    set(have_icon FALSE)
    file(REMOVE "${dir}/app.ico")
    if(R_ICON_FROM)
        # Reconfigure when the original file appears / changes.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${R_ICON_FROM}")
        if(EXISTS "${R_ICON_FROM}")
            if(NOT Python3_EXECUTABLE)
                find_package(Python3 QUIET COMPONENTS Interpreter)
            endif()
            if(Python3_EXECUTABLE)
                execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${_RAPTURE_RES_DIR}/../tools/pe_resources.py" ico
                                        "${R_ICON_FROM}" "${dir}/app.ico" ${R_ICON_GROUP}
                                RESULT_VARIABLE ico_rc OUTPUT_QUIET ERROR_VARIABLE err)
                if(ico_rc EQUAL 0)
                    set(have_icon TRUE)
                else()
                    message(WARNING "rapture_add_win32_resources(${target}): no icon extracted from ${R_ICON_FROM} (${err}); building without icon")
                endif()
            else()
                message(WARNING "rapture_add_win32_resources(${target}): Python 3 not found; building without icon")
            endif()
        else()
            message(WARNING "rapture_add_win32_resources(${target}): ${R_ICON_FROM} not found; building without icon")
        endif()
    endif()

    # ---- manifest ----
    set(dpi_xml "")
    if(R_DPI STREQUAL "SYSTEM")
        set(dpi_xml "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n    <windowsSettings>\n      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true</dpiAware>\n    </windowsSettings>\n  </application>\n")
    elseif(R_DPI STREQUAL "PERMONITOR")
        set(dpi_xml "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n    <windowsSettings>\n      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>\n      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2, PerMonitor</dpiAwareness>\n    </windowsSettings>\n  </application>\n")
    endif()
    string(REGEX MATCHALL "[0-9]+" vparts "${R_VERSION}")
    list(LENGTH vparts nv)
    while(nv LESS 4)
        list(APPEND vparts 0)
        math(EXPR nv "${nv}+1")
    endwhile()
    list(GET vparts 0 v0)
    list(GET vparts 1 v1)
    list(GET vparts 2 v2)
    list(GET vparts 3 v3)
    set(manifest
"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>
<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">
  <assemblyIdentity type=\"win32\" name=\"Rapture.${R_INTERNAL_NAME}\" version=\"${v0}.${v1}.${v2}.${v3}\" processorArchitecture=\"*\"/>
  <description>${R_DESCRIPTION}</description>
  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">
    <application>
      <supportedOS Id=\"{e2011457-1546-43c5-a5fe-008deee3d3f0}\"/>
      <supportedOS Id=\"{35138b9a-5d96-4fbd-8e2d-a2440225f93a}\"/>
      <supportedOS Id=\"{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}\"/>
      <supportedOS Id=\"{1f676c76-80e1-4239-95bb-83d0f6d0da78}\"/>
      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>
    </application>
  </compatibility>
${dpi_xml}</assembly>
")
    _rapture_write_if_changed("${dir}/app.manifest" "${manifest}")

    # ---- .rc ----
    foreach(k COMPANY DESCRIPTION FILENAME INTERNAL_NAME COPYRIGHT PRODUCT COMMENTS VERSION)
        _rapture_rc_escape(E_${k} "${R_${k}}")
    endforeach()
    set(rc "// GENERATED by rapture_add_win32_resources(${target}); do not edit.\n#include <winresrc.h>\n\n")
    if(have_icon)
        string(APPEND rc "1 ICON \"${dir}/app.ico\"\n")
    endif()
    string(APPEND rc "1 RT_MANIFEST \"${dir}/app.manifest\"\n\n")
    string(APPEND rc "VS_VERSION_INFO VERSIONINFO\n FILEVERSION ${v0},${v1},${v2},${v3}\n PRODUCTVERSION ${v0},${v1},${v2},${v3}\n FILEFLAGSMASK 0x3fL\n FILEFLAGS 0x0L\n FILEOS 0x40004L\n FILETYPE 0x1L\n FILESUBTYPE 0x0L\nBEGIN\n")
    string(APPEND rc "  BLOCK \"StringFileInfo\"\n  BEGIN\n    BLOCK \"040904B0\"\n    BEGIN\n")
    string(APPEND rc "      VALUE \"CompanyName\", \"${E_COMPANY}\"\n")
    string(APPEND rc "      VALUE \"FileDescription\", \"${E_DESCRIPTION}\"\n")
    string(APPEND rc "      VALUE \"FileVersion\", \"${E_VERSION}\"\n")
    string(APPEND rc "      VALUE \"InternalName\", \"${E_INTERNAL_NAME}\"\n")
    string(APPEND rc "      VALUE \"LegalCopyright\", \"${E_COPYRIGHT}\"\n")
    string(APPEND rc "      VALUE \"OriginalFilename\", \"${E_FILENAME}\"\n")
    string(APPEND rc "      VALUE \"ProductName\", \"${E_PRODUCT}\"\n")
    string(APPEND rc "      VALUE \"ProductVersion\", \"${E_VERSION}\"\n")
    string(APPEND rc "      VALUE \"Comments\", \"${E_COMMENTS}\"\n")
    string(APPEND rc "    END\n  END\n  BLOCK \"VarFileInfo\"\n  BEGIN\n    VALUE \"Translation\", 0x409, 1200\n  END\nEND\n")
    _rapture_write_if_changed("${dir}/app.rc" "${rc}")

    target_sources(${target} PRIVATE "${dir}/app.rc")
    if(have_icon)
        set_source_files_properties("${dir}/app.rc" PROPERTIES OBJECT_DEPENDS "${dir}/app.manifest;${dir}/app.ico")
    else()
        set_source_files_properties("${dir}/app.rc" PROPERTIES OBJECT_DEPENDS "${dir}/app.manifest")
    endif()
endfunction()
