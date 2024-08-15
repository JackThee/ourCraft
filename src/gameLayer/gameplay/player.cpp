#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include "gameplay/player.h"
#include <iostream>

void Player::flyFPS(glm::vec3 direction, glm::vec3 lookDirection)
{
	lookDirection.y = 0;
	float l = glm::length(lookDirection);

	if (!l) { return; }

	lookDirection /= l;

	//forward
	float forward = -direction.z;
	float leftRight = direction.x;
	float upDown = direction.y;

	glm::vec3 move = {};

	move += glm::vec3(0, 1, 0) * upDown;
	move += glm::normalize(glm::cross(lookDirection, glm::vec3(0, 1, 0))) * leftRight;
	move += lookDirection * forward;

	//applyImpulse(this->forces, move);
	this->position += move;
}


void Player::moveFPS(glm::vec3 direction, glm::vec3 lookDirection)
{
	lookDirection.y = 0;
	lookDirection = glm::normalize(lookDirection);

	//forward
	float forward = -direction.z;
	float leftRight = direction.x;

	glm::vec3 move = {};

	move += glm::normalize(glm::cross(lookDirection, glm::vec3(0, 1, 0))) * leftRight;
	move += lookDirection * forward;

	//applyImpulse(this->forces, move);
	this->position += move;
}

glm::vec3 Player::getColliderSize()
{
	return getMaxColliderSize();
}

glm::vec3 Player::getMaxColliderSize()
{
	return glm::vec3(0.8, 1.8, 0.8);
}


void PlayerClient::cleanup()
{
	if (skin.id)
	{
		skin.cleanup();
		skinBindlessTexture = 0;
	}
}

//todo move update here
void PlayerClient::update(float deltaTime, decltype(chunkGetterSignature) *chunkGetter)
{

}

void PlayerClient::setEntityMatrix(glm::mat4 *skinningMatrix)
{

	skinningMatrix[0] = skinningMatrix[0] * glm::toMat4(
		glm::quatLookAt(glm::normalize(entity.lookDirectionAnimation), glm::vec3(0, 1, 0)));



}
