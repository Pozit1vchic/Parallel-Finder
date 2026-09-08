# Guard: pfcore must stay Qt-free (spec: "ядро без Qt").
# Usage:
#   cmake -DQT_BAN_FILES="a.h;b.cpp" -P cmake/QtBanCheck.cmake

set(_violations "")

foreach(_file IN LISTS QT_BAN_FILES)
    if(NOT EXISTS "${_file}")
        continue()
    endif()
    file(READ "${_file}" _content)
    # Any Qt/Q* include: #include <Qt...>, #include <Q...>, #include "Q..."
    if(_content MATCHES "#[ \t]*include[ \t]*[<\"]Q")
        list(APPEND _violations "${_file}")
    endif()
endforeach()

if(_violations)
    list(JOIN _violations "\n  " _listing)
    message(FATAL_ERROR
        "Qt includes detected in Qt-free target sources (pfcore):\n  ${_listing}\n"
        "The core must not depend on Qt (see spec section 2: ядро без Qt).")
endif()
