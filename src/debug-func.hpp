#include <signal.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <iostream>

extern "C"
{
#include <wlr/util/log.h>
}

#include "api/debug.hpp"
#include "api/core.hpp"

#define max_frames 100

void print_trace()
{
    errio << "stack trace:\n";

    void* addrlist[max_frames + 1];
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
        errio << "<empty, possibly corrupt>\n";
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
                errio << symbollist[i] << ":" << funcname << "+" << begin_offset << std::endl;
            } else {
                errio << symbollist[i] << ":" << begin_name << "+" << begin_offset << std::endl;
            }
        } else {
            errio << symbollist[i] << std::endl;
        }
    }

    free(funcname);
    free(symbollist);

    exit(-1);
}

void signalHandle(int sig) {
    errio << "Crash detected!" << std::endl;
    print_trace();
    raise(SIGTRAP);
}

void vlog(log_importance_t log, const char *fmt, va_list ap)
{
    char buf[4096];
	vsnprintf(buf, 4095, fmt, ap);
    if (log == L_ERROR)
    {
        wf_debug::logfile << "[EE]";
    } else if (log == L_INFO)
    {
        wf_debug::logfile << "[II]";
    } else if (log == L_DEBUG)
    {
        wf_debug::logfile << "[DD]";
    }

    wf_debug::logfile << "[wlroots] " << buf << std::endl;
    wf_debug::logfile.flush();
}
