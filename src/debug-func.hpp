#include <signal.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <wayfire/util/log.hpp>

#include "api/debug.hpp"
#include "api/core.hpp"

#define max_frames 100

void wf_print_trace()
{
    LOGE("stack trace");

    void* addrlist[max_frames + 1];
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
        LOGE("<empty, possibly corrupt>\n");
        return;
    }

    char** symbollist = backtrace_symbols(addrlist, addrlen);

    size_t funcnamesize = 256;
    char* funcname = (char*)malloc(funcnamesize);

    for(int i = 1; i < addrlen; i++) {
        char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

        for(char* p = symbollist[i]; *p; ++p) {
            if(*p == '(')
                begin_name = p;
            else if(*p == '+')
                begin_offset = p;
            else if(*p == ')' && begin_offset){
                end_offset = p;
                break;
            }
        }

        if(begin_name && begin_offset && end_offset &&begin_name < begin_offset) {
            *begin_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            int status;
            char *ret = abi::__cxa_demangle(begin_name, funcname, &funcnamesize, &status);
            if(status == 0) {
                funcname = ret;
                LOGE(nonull(symbollist[i]), ":", nonull(funcname), "+", nonull(begin_offset));
            } else {
                LOGE(nonull(symbollist[i]), ":", nonull(begin_name), "+", nonull(begin_offset));
            }
        } else {
            LOGE(nonull(symbollist[i]));
        }
    }

    free(funcname);
    free(symbollist);
}

void signalHandle(int sig) {
    LOGE("crash detected!");
    wf_print_trace();
    raise(SIGTRAP);
}
