#include <signal.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <iostream>
#include "core.hpp"

#define max_frames 100

void print_trace() {
    error << "stack trace:\n";

    // storage array for stack trace address data
    void* addrlist[max_frames + 1];

    // retrieve current stack addresses
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
        error << "<empty, possibly corrupt>\n";
        return;
    }

    // resolve addresses into strings containing "filename(function+address)",
    // this array must be free()-ed
    char** symbollist = backtrace_symbols(addrlist, addrlen);

    //allocate string which will be filled with
    //the demangled function name
    size_t funcnamesize = 256;
    char* funcname = (char*)malloc(funcnamesize);

    // iterate over the returned symbol lines. skip the first, it is the
    // address of this function.
    for(int i = 1; i < addrlen; i++) {
        char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

        // find parentheses and +address offset surrounding the mangled name:
        // ./module(function+0x15c)[0x8048a6d]
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
                error << symbollist[i] << ":" << funcname << "+" << begin_offset << std::endl;
            } else {
                error << symbollist[i] << ":" << begin_name << "+" << begin_offset << std::endl;
            }
        } else {
            error << symbollist[i] << std::endl;
        }
    }

    free(funcname);
    free(symbollist);

    exit(-1);
}



void signalHandle(int sig) {
    error << "Crash detected!" << std::endl;
    print_trace();
}
