#ifndef VML_HOT_RELOADER_H
#define VML_HOT_RELOADER_H

#include <godot_cpp/classes/node.hpp>

#include "../core/change_watcher.h"

namespace godot {

// Polls the watched content trees every `poll_interval` seconds and forwards any
// changed files to VML.reload_resources(). Created by the game (dev builds) or
// the EditorPlugin; does nothing in release templates unless explicitly enabled.
class VMLHotReloader : public Node {
	GDCLASS(VMLHotReloader, Node)

public:
	VMLHotReloader();

	void set_poll_interval(double p_seconds);
	/// Re-seed the watcher from the base layer + every enabled mod.
	void rescan();
	void _process(double p_delta) override;

protected:
	static void _bind_methods();

private:
	vortarismodloader::ChangeWatcher watcher_;
	double poll_interval_ = 0.5;
	double elapsed_ = 0.0;
};

} // namespace godot

#endif // VML_HOT_RELOADER_H
