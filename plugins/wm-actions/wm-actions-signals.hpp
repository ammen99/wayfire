#pragma once

#include <wayfire/signal-definitions.hpp>

namespace wf
{
/**
 * name: wm-actions-toggle-above
 * on: output
 * when: Emitted whenever some entity requests that the view's above state
 *   is supposed to change.
 */
using wm_actions_toggle_above = wf::_view_signal;

/**
 * name: wm-actions-above-changed
 * on: output
 * when: Emitted whenever a views above layer has been changed.
 */
using wm_actions_above_changed = wf::_view_signal;
}
