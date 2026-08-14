#ifndef VML_MOD_LOADER_H
#define VML_MOD_LOADER_H

#include <godot_cpp/classes/node.hpp>

namespace godot {

// The VML engine singleton. Registered at MODULE_INITIALIZATION_LEVEL_SCENE so it
// exists before any autoload and before the main scene. This is the only global
// entry point mods and games talk to.
class VMLModLoader : public Node {
	GDCLASS(VMLModLoader, Node)

public:
	static void create_singleton();
	static void free_singleton();
	static VMLModLoader *get_singleton();

	VMLModLoader();
	~VMLModLoader();

	/// True once the initial scan has run (constructor). Mod scripts are only
	/// activated after finish_startup() (added in a later milestone).
	bool is_initialized() const;

protected:
	static void _bind_methods();

private:
	static VMLModLoader *singleton;
};

} // namespace godot

#endif // VML_MOD_LOADER_H
