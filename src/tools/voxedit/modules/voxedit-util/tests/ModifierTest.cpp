/**
 * @file
 */

#include "video/tests/AbstractGLTest.h"
#include "../modifier/Modifier.h"

namespace voxedit {

class ModifierTest: public video::AbstractGLTest {
private:
	using Super = video::AbstractGLTest;
protected:
	Modifier _modifier;

public:
	void SetUp() override {
		Super::SetUp();
		if (_supported) {
			ASSERT_TRUE(_modifier.init()) << "Initialization failed";
		}
	}

	void TearDown() override {
		if (_supported) {
			_modifier.shutdown();
		}
		Super::TearDown();
	}
};

TEST_F(ModifierTest, DISABLED_testModifier) {
	if (!_supported) {
		return;
	}
	// TODO: implement test
}

}
