/**
 * @file
 */
#include "Shader.h"
#include "core/Var.h"
#include "core/Log.h"
#include "core/App.h"
#include "io/Filesystem.h"

namespace compute {

Shader::~Shader() {
	shutdown();
}

bool Shader::init() {
	if (_initialized) {
		return true;
	}
	_initialized = true;
	return _initialized;
}

std::string Shader::handleIncludes(const std::string& buffer) const {
	std::string src;
	const std::string_view include = "#include";
	int index = 0;
	for (std::string::const_iterator i = buffer.begin(); i != buffer.end(); ++i, ++index) {
		const char *c = &buffer[index];
		if (*c != '#') {
			src.append(c, 1);
			continue;
		}
		if (::strncmp(include.data(), c, include.length())) {
			src.append(c, 1);
			continue;
		}
		for (; i != buffer.end(); ++i, ++index) {
			const char *cStart = &buffer[index];
			if (*cStart != '"')
				continue;

			++index;
			++i;
			for (; i != buffer.end(); ++i, ++index) {
				const char *cEnd = &buffer[index];
				if (*cEnd != '"')
					continue;

				const std::string_view dir = core::string::extractPath(_name);
				const std::string_view includeFile(cStart + 1, (size_t)(cEnd - (cStart + 1)));
				const std::string& includeBuffer = core::App::getInstance()->filesystem()->load(core::string::concat(dir, includeFile));
				if (includeBuffer.empty()) {
					Log::error("could not load shader include %s from dir %s (shader %s)", includeFile.data(), dir.data(), _name.c_str());
				}
				src.append(includeBuffer);
				break;
			}
			break;
		}
		if (i == buffer.end()) {
			break;
		}
	}
	return src;
}

void Shader::update(uint32_t deltaTime) {
	core_assert(_initialized);
}

bool Shader::activate() const {
	core_assert(_initialized);
	_active = true;
	return _active;
}

bool Shader::deactivate() const {
	if (!_active) {
		return false;
	}

	_active = false;
	return _active;
}

void Shader::shutdown() {
	_initialized = false;
	_active = false;
	compute::deleteProgram(_program);
}

bool Shader::load(const std::string& name, const std::string& buffer) {
	core_assert(_initialized);
	_name = name;
	const std::string& source = getSource(buffer);
	_program = compute::createProgram(source);
	if (_program == InvalidId) {
		return false;
	}
	return compute::configureProgram(_program);
}

Id Shader::createKernel(const char *name) {
	core_assert(_program != InvalidId);
	return compute::createKernel(_program, name);
}

void Shader::deleteKernel(Id& kernel) {
	core_assert(_initialized);
	compute::deleteKernel(kernel);
}

bool Shader::loadProgram(const std::string& filename) {
	return loadFromFile(filename + COMPUTE_POSTFIX);
}

bool Shader::loadFromFile(const std::string& filename) {
	const std::string& buffer = core::App::getInstance()->filesystem()->load(filename);
	if (buffer.empty()) {
		return false;
	}
	return load(filename, buffer);
}

std::string Shader::validPreprocessorName(const std::string& name) {
	return core::string::replaceAll(name, "_", "");
}

std::string Shader::getSource(const std::string& buffer, bool finalize) const {
	if (buffer.empty()) {
		return "";
	}
	std::string src;

	core::Var::visitSorted([&] (const core::VarPtr& var) {
		if ((var->getFlags() & core::CV_SHADER) != 0) {
			src.append("#define ");
			const std::string& validName = validPreprocessorName(var->name());
			src.append(validName);
			src.append(" ");
			std::string val;
			if (var->typeIsBool()) {
				val = var->boolVal() ? "1" : "0";
			} else {
				val = var->strVal();
			}
			src.append(val);
			src.append("\n");
		}
	});

	for (auto i = _defines.begin(); i != _defines.end(); ++i) {
		src.append("#ifndef ");
		src.append(i->first);
		src.append("\n");
		src.append("#define ");
		src.append(i->first);
		src.append(" ");
		src.append(i->second);
		src.append("\n");
		src.append("#endif\n");
	}

	src += handleIncludes(buffer);
	int level = 0;
	while (core::string::contains(src, "#include")) {
		src = handleIncludes(src);
		++level;
		if (level >= 10) {
			Log::warn("Abort shader include loop for %s", _name.c_str());
			break;
		}
	}

	core::Var::visitSorted([&] (const core::VarPtr& var) {
		if ((var->getFlags() & core::CV_SHADER) != 0) {
			const std::string& validName = validPreprocessorName(var->name());
			src = core::string::replaceAll(src, var->name(), validName);
		}
	});

	if (finalize) {
		// TODO: do placeholder/keyword replacement
	}
	return src;
}

}
