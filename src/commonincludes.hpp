#ifndef COMMON_INCLUDES
#define COMMON_INCLUDES

#include <libinput.h>


#include <sys/time.h>
#include <poll.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <memory>
#include <mutex>
#include <cstring>
#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>

#define GL_GLEXT_PROTOTYPES
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#include <fstream>

#include <wlc/wlc.h>
#include <wlc/wlc-render.h>
#include <wlc/wlc-wayland.h>

#include <linux/input.h>

#ifdef YCM
#define private public
#endif

#define MAGIC 1023444 // A random number, used for scroll up/down. Hopefully won't collide with other button!
#define BTN_SCROLL (MAGIC)

extern std::ofstream file_debug;
#define debug file_debug << "[DD] "
#define error std::cout  << "[EE] "

#endif
