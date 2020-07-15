/**
 * @file
 */

#include "SharedMovement.h"
#include "core/Log.h"
#include "core/Trace.h"
#include "core/Assert.h"
#include "voxel/Constants.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace shared {

glm::vec3 SharedMovement::calculateDelta(const glm::quat& rot) const {
	float speed = _speed;
	if (_jumping || _swimming) {
		glm::vec3 delta(0.0f);
		if (_swimming) {
			speed *= 0.2f;
		}
		if (forward()) {
			delta += rot * (glm::forward * speed);
		} else if (backward()) {
			// you can only reduce speed - but not walk backward
			delta += rot * (glm::forward * speed / 10.0f);
		}
		return delta;
	} else if (_gliding) {
		// TODO: apply a speed debuff via attributes
		glm::vec3 delta(0.0f);
		if (!forward()) {
			speed *= 0.2f;
		}
		delta += rot * (glm::forward * speed);
		return delta;
	}
	glm::vec3 delta(0.0f);
	if (left()) {
		delta += rot * (glm::left * speed);
	} else if (right()) {
		delta += rot * (glm::right * speed);
	}
	if (forward()) {
		delta += rot * (glm::forward * speed);
	} else if (backward()) {
		delta += rot * (glm::backward * speed);
	}
	return delta;
}

double SharedMovement::gravity() const {
	if (_gliding) {
		return 0.1;
	}
	if (_swimming) {
		if (forward() || backward()) {
			return -2.0;
		}
		return 2.0;
	}
	return 20.0;
}

glm::vec3 SharedMovement::update(double deltaFrameSeconds, float orientation, double speed, const glm::vec3& currentPos, const WalkableFloorResolver& heightResolver) {
	core_trace_scoped(UpdateSharedMovement);
	core_assert_msg(deltaFrameSeconds > 0.0, "Expected to get deltaFrameSeconds > 0 - but got %f", deltaFrameSeconds);
	core_assert_msg(speed > 0.0f, "Expected to get speed > 0, but got %f", speed);
	_speed = speed;
	const glm::quat& rot = glm::angleAxis(orientation, glm::up);
	glm::vec3 newPos = glm::mix(currentPos, currentPos + calculateDelta(rot), deltaFrameSeconds);

	const int maxWalkableHeight = 3;
	_floor = heightResolver(glm::ivec3(glm::floor(newPos)), maxWalkableHeight);
	if (!_floor.isValid()) {
		return currentPos;
	}
	if (_floor.heightLevel < voxel::MIN_HEIGHT) {
		_floor.heightLevel = voxel::MIN_HEIGHT;
	}
	_delay -= deltaFrameSeconds;
	const double inputDelaySeconds = 0.5;
	if (jump()) {
		if (_gliding) {
			if (_delay <= 0.0) {
				_gliding = false;
				_jumping = true;
				_delay = inputDelaySeconds;
			}
		} else if (_jumping) {
			if (_delay <= 0.0) {
				_jumping = false;
				_gliding = true;
				_fallingVelocity = 0.0;
				_delay = inputDelaySeconds;
			}
		} else {
			_fallingVelocity = 10.0;
			_jumping = true;
			_delay = inputDelaySeconds;
		}
	}
	if (_gliding) {
		_fallingVelocity = -gravity();
	} else {
		_fallingVelocity -= gravity() * deltaFrameSeconds;
	}
	newPos.y += _fallingVelocity * deltaFrameSeconds;
	if (newPos.y <= _floor.heightLevel) {
		newPos.y = _floor.heightLevel;
		_fallingVelocity = 0.0;
		_jumping = false;
		_gliding = false;
		_delay = 0.0;
	}

	if (newPos.y < voxel::MAX_WATER_HEIGHT) {
		if (voxel::MAX_WATER_HEIGHT - _floor.heightLevel > 2) {
			_fallingVelocity = 0.0;
			_jumping = false;
			_gliding = false;
			_delay = 0.0;
			_swimming = true;
			_inWater = true;
			_fallingVelocity = -2.0;
		} else {
			_swimming = false;
			_inWater = true;
		}
	} else {
		_swimming = false;
		_inWater = false;
	}
	return newPos;
}

network::Animation SharedMovement::animation() const {
	if (_swimming) {
		if (backward() || forward()) {
			return network::Animation::SWIM;
		}
		return network::Animation::IDLE;
	}
	if (_gliding) {
		return network::Animation::GLIDE;
	}
	if (_jumping) {
		return network::Animation::JUMP;
	}
	if (moving()) {
		return network::Animation::RUN;
	}
	return network::Animation::IDLE;
}

}
