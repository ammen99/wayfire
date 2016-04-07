#include "core.hpp"
#include "output.hpp"

std::ofstream file_debug;

#include <signal.h>
#include <execinfo.h>
#include <cxxabi.h>

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

bool output_created(wlc_handle output) {
    debug << "output created id:" << output << std::endl;
    return true;
}

/* TODO: handle this, move all views from this output and disable it */
void output_destroyed(wlc_handle output) {
    debug << "output destroyed id:" << output << std::endl;
}

void output_ctx_created(wlc_handle output) {
    debug << "output context created id:" << output << std::endl;
    Output *o = core->get_output(output);
    if (!o)
        return;

    if (o->render->ctx)
        delete o->render->ctx;

    o->render->load_context();
}

void output_ctx_destroyed(wlc_handle output) {
    debug << "output context destroyed id:" << output << std::endl;
}

void output_pre_paint(wlc_handle output) {
    assert(core);

    /* TODO: format this */
    Output *o;
    if (!(o = core->get_output(output))) {
        core->add_output(output);
        o = core->get_output(output);
        o->render->load_context();
    } else {
        o->render->paint();
    }
}

void output_post_paint(wlc_handle output) {
    assert(core);

    auto o = core->get_output(output);
    if (!o) return;

    o->render->post_paint();
    o->hook->run_hooks();
    if (o->should_redraw()) {
        wlc_output_schedule_render(output);
        o->for_each_view([] (View v) {
            wlc_surface_flush_frame_callbacks(v->get_surface());
        });
    }
}

void output_focus(wlc_handle output, bool is_focused) {
}


bool keyboard_key(wlc_handle view, uint32_t time,
        const struct wlc_modifiers *modifiers, uint32_t key,
        enum wlc_key_state state) {
    uint32_t sym = wlc_keyboard_get_keysym_for_key(key, NULL);

    Output *output = core->get_active_output();
    bool grabbed = output->input->process_key_event(sym, modifiers->mods, state);

    if (output->should_redraw())
        wlc_output_schedule_render(output->get_handle());

    return grabbed;
}

bool pointer_button(wlc_handle view, uint32_t time,
        const struct wlc_modifiers *modifiers, uint32_t button,
        enum wlc_button_state state, const struct wlc_point *position) {

    Output *output = core->get_active_output();
    bool grabbed = output->input->process_button_event(button, modifiers->mods, state, *position);
    if (output->should_redraw())
        wlc_output_schedule_render(output->get_handle());

    return grabbed;
}

bool pointer_motion(wlc_handle view, uint32_t time, const struct wlc_point *position) {
    wlc_pointer_set_position(position);

    auto output = core->get_active_output();
    bool grabbed = output->input->process_pointer_motion_event(*position);

    if (output->should_redraw())
        wlc_output_schedule_render(output->get_handle());

    return grabbed;
}

bool on_scroll(wlc_handle view, uint32_t time, const struct wlc_modifiers* mods,
        uint8_t axis, double amount[2]) {

    auto output = core->get_active_output();
    if (output) {
        return output->input->process_scroll_event(mods->mods, amount);
    } else {
        return false;
    }
}

bool view_created(wlc_handle view) {
    debug << "view created id:" << view << std::endl;
    core->add_view(view);
    return true;
}

void view_destroyed(wlc_handle view) {
    debug << "view destroyed id:" << view << std::endl;
    core->rem_view(view);
    core->focus_view(core->get_active_output()->get_active_view());
}

void view_focus(wlc_handle view, bool focus) {
    debug << "view focus id:" << view << std::endl;
//    wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
    if (focus && false)
        wlc_view_bring_to_front(view);
}

void view_request_resize(wlc_handle view, uint32_t edges, const struct wlc_point *origin) {
    debug << "view request resize id:" << view << std::endl;
    SignalListenerData data;

    auto v = core->find_view(view);
    if (!v)
       return;

    data.push_back((void*)(&v));
    data.push_back((void*)(origin));

    v->output->signal->trigger_signal("resize-request", data);
}


void view_request_move(wlc_handle view, const struct wlc_point *origin) {
    debug << "view request move id:" << view << std::endl;

    auto v = core->find_view(view);
    if (!v) return;
    SignalListenerData data;
    data.push_back((void*)(&v));
    data.push_back((void*)(origin));

    v->output->signal->trigger_signal("move-request", data);
}

void log(wlc_log_type type, const char *msg) {
    debug << "[wlc] " << msg << std::endl;
}

void view_request_geometry(wlc_handle view, const wlc_geometry *g) {
    debug << "view request geometry id:" << view << std::endl;
    auto v = core->find_view(view);
    if (!v) return;

    /* TODO: add pending changes for views that are not visible */
    //if(v->is_visible() || v->default_mask == 0) {
        v->set_geometry(g->origin.x, g->origin.y, g->size.w, g->size.h);
     //   v->set_mask(v->output->viewport->get_mask_for_view(v));
    //}
}

void view_request_state(wlc_handle view, wlc_view_state_bit state, bool toggle) {
    debug << "view request state id:" << view << std::endl;
    /* TODO: change size of fullscreen/maximized views */
    wlc_view_set_state(view, state, toggle);
}

void view_move_to_output(wlc_handle view, wlc_handle old, wlc_handle new_output) {
    debug << "view id:" << view << " moved from output " << old << " to " << new_output << std::endl;
    core->move_view_to_output(core->find_view(view), core->get_output(old), core->get_output(new_output));
}

void on_activate() {
    core->for_each_output([] (Output *o) {o->activate();});
}

void on_deactivate() {
    core->for_each_output([] (Output *o) {o->deactivate();});
}

int main(int argc, char *argv[]) {
    std::streambuf *save;

    /* setup logging */
    /* first argument is a log file */
    if (argc > 1) {
        file_debug.open(argv[1]);
        save = std::cout.rdbuf();
        std::cout.rdbuf(file_debug.rdbuf());
    } else {
        file_debug.open("/dev/null");
    }
    wlc_log_set_handler(log);

    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);
    signal(SIGTRAP, signalHandle);

    wlc_set_view_created_cb       (view_created);
    wlc_set_view_destroyed_cb     (view_destroyed);
    wlc_set_view_focus_cb         (view_focus);
    wlc_set_view_move_to_output_cb(view_move_to_output);

    wlc_set_view_request_resize_cb(view_request_resize);
    wlc_set_view_request_move_cb(view_request_move);
    wlc_set_view_request_geometry_cb(view_request_geometry);
    wlc_set_view_request_state_cb(view_request_state);

    wlc_set_output_created_cb(output_created);
    wlc_set_output_destroyed_cb(output_destroyed);
    wlc_set_output_focus_cb(output_focus);

    wlc_set_output_render_pre_cb(output_pre_paint);
    wlc_set_output_render_post_cb(output_post_paint);

    wlc_set_output_context_created_cb(output_ctx_created);
    wlc_set_output_context_destroyed_cb(output_ctx_destroyed);

    wlc_set_keyboard_key_cb(keyboard_key);
    wlc_set_pointer_scroll_cb(on_scroll);
    wlc_set_pointer_button_cb(pointer_button);
    wlc_set_pointer_motion_cb(pointer_motion);

    core = new Core();
    core->init();

    if (!wlc_init2())
        return EXIT_FAILURE;

    wlc_run();

    std::cout.rdbuf(save);
    return EXIT_SUCCESS;
}
