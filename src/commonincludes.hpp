#ifndef COMMON_INCLUDES
#define COMMON_INCLUDES

#include <libinput.h>


#include <sys/time.h>
#include <poll.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdlib>
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
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define ILUT_USE_OPENGL
#include <IL/ilut.h>

#include <fstream>

extern "C"{
#include <wlc/wlc.h>
#include <wlc/wlc-render.h>
#include <wlc/wlc-wayland.h>
}
#include <linux/input.h>

#ifdef YCM
#define private public
#endif

#define AllModifiers (ControlMask | ShiftMask | Mod1Mask \
        | Mod2Mask | Mod3Mask | Mod4Mask)

#endif

