/**
 * @file
 */

#pragma once

#include "Common.h"
#include "String.h"

namespace core {

class Tokenizer {
protected:
	std::vector<std::string> _tokens;
	std::size_t _posIndex;
	std::size_t _size;

	char skip(const char **s) const;
public:
	Tokenizer(const std::string& string);

	inline bool hasNext() const {
		return _posIndex < _tokens.size();
	}

	inline const std::string& next() {
		core_assert(hasNext());
		return _tokens[_posIndex++];
	}

	inline bool hasPrev() const {
		return _posIndex > 0;
	}

	inline std::size_t size() const {
		return _tokens.size();
	}

	/**
	 * @return the current position in the tokens
	 */
	inline std::size_t pos() const {
		return _posIndex;
	}

	inline const std::string& prev() {
		core_assert(hasPrev());
		return _tokens[--_posIndex];
	}
};

}
