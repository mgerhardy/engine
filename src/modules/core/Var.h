/**
 * @file
 */

#pragma once

#include "core/concurrent/ReadWriteLock.h"
#include "GameConfig.h"
#include "core/SharedPtr.h"
#include "core/String.h"
#include "core/collection/Map.h"
#include "core/collection/DynamicArray.h"
#include <string.h>
#include <glm/fwd.hpp>

namespace core {

/**
 * @defgroup Var
 * @{
 */

/** @brief Variable may only be modified at application start via command line */
const uint32_t CV_READONLY = 1 << 0;
/** @brief will not get saved to the file */
const uint32_t CV_NOPERSIST = 1 << 1;
/** @brief will be put as define in every shader - a change will update the shaders at runtime */
const uint32_t CV_SHADER = 1 << 2;
/** @brief will be broadcasted to all connected clients */
const uint32_t CV_REPLICATE = 1 << 3;
/** @brief user information that will be sent out to all connected clients (e.g. user name) */
const uint32_t CV_BROADCAST = 1 << 4;
/** @brief don't show the value to users, but just ***secure*** it out */
const uint32_t CV_SECRET = 1 << 5;
const uint32_t CV_PRESERVE = (CV_READONLY | CV_NOPERSIST | CV_SHADER | CV_REPLICATE | CV_BROADCAST | CV_SECRET);

const uint32_t CV_FROMFILE = 1 << 6;
const uint32_t CV_FROMCOMMANDLINE = 1 << 7;
const uint32_t CV_FROMENV = 1 << 8;

extern const core::String VAR_TRUE;
extern const core::String VAR_FALSE;

class Var;
typedef core::SharedPtr<Var> VarPtr;

/**
 * @brief A var can be changed and queried at runtime
 *
 * Create a new variable with all parameters
 * @code
 * core::VarPtr var = core::Var::get("prefix_name", "defaultvalue", 0);
 * @endcode
 *
 * If you just want to get an existing variable use:
 * @code
 * core::Var::get("prefix_name");
 * @endcode
 */
class Var {
protected:
	friend class SharedPtr<Var>;
	typedef Map<core::String, VarPtr, 64, core::StringHash> VarMap;
	static VarMap _vars;
	static ReadWriteLock _lock;

	core::VarPtr _volume;
	core::VarPtr _musicVolume;

	const core::String _name;
	const char* _help = nullptr;
	uint32_t _flags;
	static constexpr int NEEDS_REPLICATE = 1 << 0;
	static constexpr int NEEDS_BROADCAST = 1 << 1;
	uint8_t _updateFlags = 0u;

	static uint8_t _visitFlags;

	struct Value {
		float _floatValue = 0.0f;
		int _intValue = 0;
		long _longValue = 0l;
		core::String _value;
	};

	core::DynamicArray<Value> _history;
	uint32_t _currentHistoryPos = 0;
	bool _dirty;

	void addValueToHistory(const core::String& value);

	// invisible - use the static get method
	Var(const core::String& name, const core::String& value = "", uint32_t flags = 0u, const char *help = nullptr);
public:
	~Var();

	/**
	 * @brief Creates a new or gets an already existing var
	 *
	 * @param[in] name The name that this var is registered under (must be unique)
	 * @param[in] value The initial value of the var. If this is @c nullptr a new cvar
	 * is not created by this call.
	 * @param[in] flags A bitmask of var flags - e.g. @c CV_READONLY
	 *
	 * @note This is using a read/write lock to allow access from different threads.
	 */
	static VarPtr get(const core::String& name, const char* value = nullptr, int32_t flags = -1, const char *help = nullptr);

	static inline VarPtr get(const core::String& name, const char* value, const char *help) {
		return get(name, value, -1, help);
	}

	static inline VarPtr get(const core::String& name, const String& value, int32_t flags = -1, const char *help = nullptr) {
		return get(name, value.c_str(), flags, help);
	}

	/**
	 * @note Same as get(), but uses @c core_assert if no var could be found with the given name.
	 */
	static VarPtr getSafe(const core::String& name);

	/**
	 * @return @c empty string if var with given name wasn't found, otherwise the value of the var
	 */
	static core::String str(const core::String& name);

	/**
	 * The memory is now owned. Make sure it is available for the whole lifetime of this instance.
	 */
	void setHelp(const char *help);

	const char *help() const;

	/**
	 * @return @c false if var with given name wasn't found, otherwise the bool value of the var
	 */
	static bool boolean(const core::String& name);

	static VarPtr get(const core::String& name, int value, int32_t flags = -1);

	static void shutdown();

	template<class Functor>
	static void visit(Functor&& func) {
		Var::VarMap varList;
		{
			ScopedReadLock lock(_lock);
			varList = _vars;
		}
		for (auto i = varList.begin(); i != varList.end(); ++i) {
			func(i->second);
		}
	}

	template<class Functor>
	static void visitDirtyBroadcast(Functor&& func) {
		if ((_visitFlags & NEEDS_BROADCAST) == 0) {
			return;
		}
		_visitFlags &= ~NEEDS_BROADCAST;

		visit([&] (const VarPtr& var) {
			if (var->_updateFlags & NEEDS_BROADCAST) {
				func(var);
				var->_updateFlags &= ~NEEDS_BROADCAST;
			}
		});
	}

	template<class Functor>
	static void visitBroadcast(Functor&& func) {
		visit([&] (const VarPtr& var) {
			if (var->_flags & CV_BROADCAST) {
				func(var);
			}
		});
	}

	template<class Functor>
	static void visitDirtyReplicate(Functor&& func) {
		if ((_visitFlags & NEEDS_REPLICATE) == 0) {
			return;
		}
		_visitFlags &= ~NEEDS_REPLICATE;
		visit([&] (const VarPtr& var) {
			if (var->_updateFlags & NEEDS_REPLICATE) {
				func(var);
				var->_updateFlags &= ~NEEDS_REPLICATE;
			}
		});
	}

	template<class Functor>
	static void visitReplicate(Functor&& func) {
		visit([&] (const VarPtr& var) {
			if (var->_flags & CV_REPLICATE) {
				func(var);
			}
		});
	}

	template<class Functor>
	static bool check(Functor&& func) {
		Var::VarMap varList;
		{
			ScopedReadLock lock(_lock);
			varList = _vars;
		}
		for (auto i = varList.begin(); i != varList.end(); ++i) {
			if (func(i->second)) {
				return true;
			}
		}
		return false;
	}

	void clearHistory();
	uint32_t getHistorySize() const;
	uint32_t getHistoryIndex() const;
	bool useHistory(uint32_t historyIndex);

	/**
	 * @return the bitmask of flags for this var
	 * @note See the existing @c CV_ ints
	 */
	uint32_t getFlags() const;
	/**
	 * @return the value of the variable as @c int.
	 *
	 * @note There is no conversion happening here - this is done in @c Var::setVal
	 */
	int intVal() const;
	/**
	 * @return the value of the variable as @c unsigned int.
	 *
	 * @note There is no conversion happening here - this is done in @c Var::setVal
	 */
	unsigned int uintVal() const;
	/**
	 * @return the value of the variable as @c long.
	 *
	 * @note There is no conversion happening here - this is done in @c Var::setVal
	 */
	long longVal() const;
	unsigned long ulongVal() const;
	/**
	 * @return the value of the variable as @c float.
	 *
	 * @note There is no conversion happening here - this is done in @c Var::setVal
	 */
	float floatVal() const;
	/**
	 * @return the value of the variable as @c bool. @c true if the string value is either @c 1 or @c true, @c false otherwise
	 */
	bool boolVal() const;
	glm::vec3 vec3Val() const;
	void setVal(const core::String& value);
	inline void setVal(const char* value) {
		if (!SDL_strcmp(_history[_currentHistoryPos]._value.c_str(), value)) {
			return;
		}
		setVal(core::String(value));
	}
	inline void setVal(bool value) {
		if (boolVal() == value) {
			return;
		}
		setVal(value ? VAR_TRUE : VAR_FALSE);
	}
	void setVal(int value);
	void setVal(float value);
	/**
	 * @return The string value of this var
	 */
	const core::String& strVal() const;
	const core::String& name() const;
	/**
	 * @return @c true if some @c Var::setVal call changed the initial/default value that was specified on construction
	 */
	bool isDirty() const;
	void markClean();

	bool typeIsBool() const;
};

inline uint32_t Var::getHistorySize() const {
	return (uint32_t)_history.size();
}

inline uint32_t Var::getHistoryIndex() const {
	return _currentHistoryPos;
}

inline void Var::clearHistory() {
	if (_history.size() == 1u) {
		return;
	}
	_history.release();
}

inline float Var::floatVal() const {
	return _history[_currentHistoryPos]._floatValue;
}

inline int Var::intVal() const {
	return _history[_currentHistoryPos]._intValue;
}

inline long Var::longVal() const {
	return _history[_currentHistoryPos]._longValue;
}

inline unsigned long Var::ulongVal() const {
	return static_cast<unsigned long>(_history[_currentHistoryPos]._longValue);
}

inline bool Var::boolVal() const {
	return _history[_currentHistoryPos]._value == "true" || _history[_currentHistoryPos]._value == "1";
}

inline bool Var::typeIsBool() const {
	return _history[_currentHistoryPos]._value == "true" || _history[_currentHistoryPos]._value == "1" || _history[_currentHistoryPos]._value == "false" || _history[_currentHistoryPos]._value == "0";
}

inline const core::String& Var::strVal() const {
	return _history[_currentHistoryPos]._value;
}

inline const core::String& Var::name() const {
	return _name;
}

inline bool Var::isDirty() const {
	return _dirty;
}

inline void Var::markClean() {
	_dirty = false;
}

inline uint32_t Var::getFlags() const {
	return _flags;
}

inline unsigned int Var::uintVal() const {
	return static_cast<unsigned int>(_history[_currentHistoryPos]._intValue);
}

inline void Var::setHelp(const char *help) {
	_help = help;
}

inline const char *Var::help() const {
	return _help;
}

/**
 * @}
 */

}
