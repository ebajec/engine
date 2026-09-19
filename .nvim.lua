-- Shaders include and import by vfs path ("core/shader/frame.glsl",
-- import "core/shader/sort/sort.slang"). Those resolve against
-- build/shader_include, which CMake creates at configure time and
-- compile_shaders.sh keeps up to date, so the tools that read shaders need it
-- as a search path, same as the shader compiler.
--
-- glsl_analyzer is not configured here: it only resolves includes relative to
-- the including file, so symbols from included headers are unavailable in it.
-- The glslc lint below reports cross-file errors instead.

local root = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h")
local include_root = root .. "/build/shader_include"

if not vim.uv.fs_stat(include_root) then
	vim.notify(
		include_root .. " does not exist; configure the project with CMake so shader includes resolve",
		vim.log.levels.WARN
	)
end

-- slangd: merged over the base config in the user's lsp/slang.lua
vim.lsp.config("slang", {
	settings = {
		slang = {
			additionalSearchPaths = { include_root },
		},
	},
})

-- nvim-lint's glslc: same include root and target as compile_shaders.sh
local ok, lint = pcall(require, "lint")
if ok then
	local glslc = lint.linters.glslc
	if type(glslc) == "table" then
		glslc.args = vim.list_extend(
			{ "-I" .. include_root, "--target-env=vulkan1.3" },
			glslc.args or {}
		)
	end
end
