project(backlight)

add_library(backlight SHARED "backlight.cpp")
install(TARGETS backlight DESTINATION lib/wayfire/)

add_executable(intel-util "intel-util.cpp")
install(TARGETS intel-util
    DESTINATION lib/wayfire
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID)
