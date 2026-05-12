#pragma once

#include "math.hpp"

namespace sky {

	bool init(const char* cubemap_path);
	void shutdown();
	void render(mat4 inv_view_proj);

}
