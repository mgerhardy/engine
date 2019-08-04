/**
 * @file
 */

#include "core/tests/AbstractTest.h"
#include "util/KeybindingHandler.h"
#include "core/command/Command.h"
#include <unordered_map>

namespace util {

namespace keybindingtest {
static const std::string CFG = R"(
w +foo
left_alt+w "somecommand +"
RIGHT_CTRL+a +bar
CTRL+w +bar
SHIFT+w +xyz
SHIFT+ctrl+ALT+w allmodscommand
ctrl+SHIFT+w ctrlshiftmodcommand
left_alt altmodcommand
)";
}

class KeybindingHandlerTest : public core::AbstractTest {
protected:
	KeybindingParser _parser;
	bool _allmodscommand = false;
	bool _ctrlshiftmodcommand = false;
	bool _somecommand = false;
	bool _altmodcommand = false;

	bool _xyz = false;

	KeybindingHandlerTest() :
			_parser(keybindingtest::CFG) {
	}

	bool onInitApp() override {
		if (!core::AbstractTest::onInitApp()) {
			return false;
		}
		if (_parser.invalidBindings() > 0) {
			Log::error("Not all bindings could get parsed");
			return false;
		}
		_xyz = _ctrlshiftmodcommand = _somecommand = _altmodcommand = _allmodscommand = false;
		core::Command::shutdown();
		core::Command::registerCommand("+bar", [] (const core::CmdArgs& args) {});
		core::Command::registerCommand("+foo", [] (const core::CmdArgs& args) {});
		core::Command::registerCommand("+xyz", [this] (const core::CmdArgs& args) {this->_xyz = true;});
		core::Command::registerCommand("somecommand", [this] (const core::CmdArgs& args) {this->_somecommand = true;});
		core::Command::registerCommand("altmodcommand", [this] (const core::CmdArgs& args) {this->_altmodcommand = true;});
		core::Command::registerCommand("allmodscommand", [this] (const core::CmdArgs& args) {this->_allmodscommand = true;});
		core::Command::registerCommand("ctrlshiftmodcommand", [this] (const core::CmdArgs& args) {this->_ctrlshiftmodcommand = true;});
		return true;
	}

	static inline std::string toModString(int16_t mod) {
		if (mod == 0) {
			return "[none]";
		}
		std::string buf;
		buf.reserve(1024);
		buf.append("[");
		int m = 0;
		if ((mod & KMOD_ALT) == KMOD_ALT) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("alt");
			++m;
		} else if ((mod & KMOD_LALT) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("left_alt");
			++m;
		} else if ((mod & KMOD_RALT) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("right_alt");
			++m;
		}

		if ((mod & KMOD_SHIFT) == KMOD_SHIFT) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("shift");
			++m;
		} else if ((mod & KMOD_LSHIFT) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("left_shift");
			++m;
		} else if ((mod & KMOD_RSHIFT) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("right_shift");
			++m;
		}

		if ((mod & KMOD_CTRL) == KMOD_CTRL) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("ctrl");
			++m;
		} else if ((mod & KMOD_LCTRL) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("left_ctrl");
			++m;
		} else if ((mod & KMOD_RCTRL) != 0) {
			if (m > 0) {
				buf.append(" ");
			}
			buf.append("right_ctrl");
			++m;
		}
		buf.append("]");
		return buf;
	}

	void execute(int32_t key, int16_t modifier = KMOD_NONE) {
		EXPECT_TRUE(util::executeCommandsForBinding(_parser.getBindings(), key, modifier))
				<< "Command for key '" << SDL_GetKeyName(key) << "' with pressed modifiers " << toModString(modifier) << " should be executed";
	}

	void notExecute(int32_t key, int16_t modifier = KMOD_NONE) {
		EXPECT_FALSE(util::executeCommandsForBinding(_parser.getBindings(), key, modifier))
				<< "Command for key '" << SDL_GetKeyName(key) << "' with pressed modifiers " << toModString(modifier) << " should not be executed";
	}

	/**
	 * for +commandname bindings
	 */
	void executeActionButtonCommand(int32_t key, int16_t modifier = KMOD_NONE) {
		execute(key, modifier);
	}
};

TEST_F(KeybindingHandlerTest, testValidCommandNoModifiers) {
	executeActionButtonCommand(SDLK_w);
}

TEST_F(KeybindingHandlerTest, testNotBoundKey) {
	notExecute(SDLK_b);
}

TEST_F(KeybindingHandlerTest, testLeftAltModifier) {
	execute(SDLK_w, KMOD_LALT);
	EXPECT_TRUE(_somecommand) << "expected command wasn't executed";
	EXPECT_FALSE(_allmodscommand) << "unexpected command was executed";
}

TEST_F(KeybindingHandlerTest, testRightAltModifier) {
	notExecute(SDLK_w, KMOD_RALT);
}

TEST_F(KeybindingHandlerTest, testAltKey) {
	execute(SDLK_LALT, KMOD_NONE);
	EXPECT_TRUE(_altmodcommand) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testLeftShiftModifier) {
	execute(SDLK_w, KMOD_LSHIFT);
	EXPECT_TRUE(_xyz) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testAllValidModifier) {
	execute(SDLK_w, KMOD_LSHIFT | KMOD_LCTRL | KMOD_LALT);
	EXPECT_TRUE(_allmodscommand) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testAllValidModifier2) {
	execute(SDLK_w, KMOD_RSHIFT | KMOD_LCTRL | KMOD_RALT);
	EXPECT_TRUE(_allmodscommand) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testCtrlShiftModifier) {
	execute(SDLK_w, KMOD_LSHIFT | KMOD_LCTRL);
	EXPECT_TRUE(_ctrlshiftmodcommand) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testLShiftRCtrlModifier) {
	execute(SDLK_w, KMOD_LSHIFT | KMOD_RCTRL);
	EXPECT_TRUE(_ctrlshiftmodcommand) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testRightShiftModifier) {
	execute(SDLK_w, KMOD_RSHIFT);
	EXPECT_TRUE(_xyz) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testShiftModifier) {
	execute(SDLK_w, KMOD_LSHIFT);
	EXPECT_TRUE(_xyz) << "expected command wasn't executed";
}

TEST_F(KeybindingHandlerTest, testCtrlModifierA) {
	executeActionButtonCommand(SDLK_a, KMOD_RCTRL);
}

TEST_F(KeybindingHandlerTest, testCtrlModifierAWrongModifierPressed) {
	notExecute(SDLK_a, KMOD_LCTRL);
}

TEST_F(KeybindingHandlerTest, testCtrlModifier) {
	executeActionButtonCommand(SDLK_w, KMOD_LCTRL);
}

TEST_F(KeybindingHandlerTest, testModifierMasksAltSimple) {
	EXPECT_TRUE(isValidForBinding(KMOD_LALT, "lalt pressed - alt bound", KMOD_ALT));
	EXPECT_TRUE(isValidForBinding(KMOD_RALT, "ralt pressed - alt bound", KMOD_ALT));

	EXPECT_TRUE(isValidForBinding(KMOD_LALT, "lalt pressed - lalt bound", KMOD_LALT));
	EXPECT_FALSE(isValidForBinding(KMOD_RALT, "ralt pressed - lalt bound", KMOD_LALT));
}

TEST_F(KeybindingHandlerTest, testModifierMasksShiftSimple) {
	EXPECT_TRUE(isValidForBinding(KMOD_LSHIFT, "lshift pressed - shift bound", KMOD_SHIFT));
	EXPECT_TRUE(isValidForBinding(KMOD_RSHIFT, "rshift pressed - shift bound", KMOD_SHIFT));

	EXPECT_TRUE(isValidForBinding(KMOD_LSHIFT, "lshift pressed - lshift bound", KMOD_LSHIFT));
	EXPECT_FALSE(isValidForBinding(KMOD_RSHIFT, "rshift pressed - lshift bound", KMOD_LSHIFT));
}

TEST_F(KeybindingHandlerTest, testModifierMasksCtrlSimple) {
	EXPECT_TRUE(isValidForBinding(KMOD_LCTRL, "lctrl pressed - ctrl bound", KMOD_CTRL));
	EXPECT_TRUE(isValidForBinding(KMOD_RCTRL, "rctrl pressed - ctrl bound", KMOD_CTRL));

	EXPECT_TRUE(isValidForBinding(KMOD_LCTRL, "lctrl pressed - lctrl bound", KMOD_LCTRL));
	EXPECT_FALSE(isValidForBinding(KMOD_RCTRL, "rctrl pressed - lctrl bound", KMOD_LCTRL));
}

TEST_F(KeybindingHandlerTest, testModifierMasksInvalidModifiers) {
	EXPECT_TRUE(isValidForBinding(KMOD_LALT | KMOD_NUM, "lalt pressed - alt bound", KMOD_ALT));
	EXPECT_TRUE(isValidForBinding(KMOD_RALT | KMOD_NUM, "ralt pressed - alt bound", KMOD_ALT));

	EXPECT_TRUE(isValidForBinding(KMOD_LALT | KMOD_NUM, "lalt pressed - lalt bound", KMOD_LALT));
	EXPECT_FALSE(isValidForBinding(KMOD_RALT | KMOD_NUM, "ralt pressed - lalt bound", KMOD_LALT));
}

}
