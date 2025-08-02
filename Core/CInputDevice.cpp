/*
  Daidalos Engine
  Copyright (C) 2011-2025 Stéphane Baudoux daidalos@stephane-baudoux.com

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
	 claim that you wrote the original software. If you use this software
	 in a product, an acknowledgment in the product documentation would be
	 appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
	 misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <algorithm>
#include "../Core/Memory/CExceptionAssert.hpp"
#include "../Core/Logger/CScopeTracer.hpp"
#include "../Core/CInputDevice.hpp"

namespace daidalos {

	namespace core {

		CInputDevice::CInputDevice() {
			LOG_TRACE;
		}

		void CInputDevice::AddAction(const SInputActionDeclaration& declaration) {
			LOG_TRACE;

			SInputAction action;
			action.state = EInputKeyState::INPUT_KEY_STATE_DISABLED;
			action.actionFactor = 1;

			action.actionId = declaration.actionId;
			action.linkedActionId = declaration.linkedActionId;

			// Main actions
			this->m_actions[declaration.actionId] = action;

			// Gamepad actions
			for (auto& it : this->m_gamepadActions) {
				it[declaration.actionId] = action;
			}
		}

		void CInputDevice::SetActionKey(const unsigned int action, const EInputKey key) {
			LOG_TRACE;
			Assert(this->m_actions.find(action) != this->m_actions.end());

			SInputKey tmp{};
			if (key >= EInputKey::INPUT_KEY_UNDEFINED && key < EInputKey::INPUT_KEY_KEYBOARD_COMPOSE) {
				tmp.type = EInputKeyType::INPUT_TYPE_KEYBOARD;
			}
			else {
				tmp.type = EInputKeyType::INPUT_TYPE_MOUSE;
			}
			tmp.key = key;

			this->m_actions[action].keys.push_back(tmp);
			this->m_actions[action].state = EInputKeyState::INPUT_KEY_STATE_UP;
		}

		void CInputDevice::SetGamepadActionKey(const unsigned int action, const unsigned int gamepadId, const EInputKey key) {
			LOG_TRACE;
			Assert(this->m_gamepadActions[gamepadId].find(action) != this->m_gamepadActions[gamepadId].end());

			SInputKey tmp{};
			tmp.type = EInputKeyType::INPUT_TYPE_GAMEPAD;
			tmp.key = key;

			this->m_gamepadActions[gamepadId][action].keys.push_back(tmp);
			this->m_gamepadActions[gamepadId][action].state = EInputKeyState::INPUT_KEY_STATE_UP;
		}

		void CInputDevice::AddKeyboardListener(IInputKeyboardListener* listener) {
			LOG_TRACE;
			this->m_keyboardListeners.push_back(listener);
		}

		void CInputDevice::RemoveKeyboardListener(IInputKeyboardListener* listener) {
			LOG_TRACE;

			const auto it = std::find(this->m_keyboardListeners.begin(), this->m_keyboardListeners.end(), listener);
			if (it != this->m_keyboardListeners.end()) {
				this->m_keyboardListeners.erase(it);
			}
		}

		void CInputDevice::AddMouseListener(IInputMouseListener* listener) {
			LOG_TRACE;
			this->m_mouseListeners.push_back(listener);
		}

		void CInputDevice::AddGamepadListener(const unsigned int gamepadId, IInputGamepadListener* listener) {
			LOG_TRACE;
			this->m_gamepadListeners[gamepadId].push_back(listener);
		}

		void CInputDevice::ClearActionKeys(const unsigned int action, EInputKeyType type, const int index) {
			LOG_TRACE;
			Assert(this->m_actions.find(action) != this->m_actions.end());

			for (auto it = this->m_actions[action].keys.begin(); it != this->m_actions[action].keys.end(); ++it) {
				this->m_actions[action].keys.erase(it);
			}
		}

		void CInputDevice::MoveMouse(const CVector2I& position, const CVector2I& movement) {
			LOG_TRACE;

			this->m_mousePosition = position;
			this->m_mouseMovement += movement;

			// Call the listeners
			for (auto& mouseListener : this->m_mouseListeners) {
				mouseListener->OnInputMouseMoved(this->m_mousePosition, movement);
			}
		}

		void CInputDevice::SetMousePosition(const CVector2I& position, const bool triggerEvent) {
			LOG_TRACE;

			if (triggerEvent) {
				this->m_mouseMovement += position - this->m_mousePosition;
				this->m_mousePosition = position;

				// Call the listeners
				for (auto& mouseListener : this->m_mouseListeners) {
					mouseListener->OnInputMouseMoved(this->m_mousePosition, position - this->m_mousePosition);
				}
			}
			else {
				this->m_mousePosition = position;
			}
		}

		void CInputDevice::SetMouseMotion(const CVector2I& movement) {
			LOG_TRACE;

			this->m_mouseMovement = movement;
			this->m_mousePosition += movement;

			// Call the listeners
			for (auto& mouseListener : this->m_mouseListeners) {
				mouseListener->OnInputMouseMoved(this->m_mousePosition, this->m_mouseMovement);
			}
		}

		void CInputDevice::SetKeyboardKeyUp(const EInputKey key, const unsigned int ascii) {
			LOG_TRACE;

			SetKeyUp(this->m_actions, EInputKeyType::INPUT_TYPE_KEYBOARD, key);

			// Call all the listeners
			for (auto& keyboardListener : this->m_keyboardListeners) {
				keyboardListener->OnKeyPressed(ascii);
			}
		}

		void CInputDevice::SetKeyboardKeyDown(const EInputKey key, const unsigned int ascii) {
			LOG_TRACE;

			SetKeyDown(this->m_actions, EInputKeyType::INPUT_TYPE_MOUSE, key);
		}

		void CInputDevice::SetMouseKeyUp(const EInputKey key, const unsigned int ascii) {
			LOG_TRACE;

			SetKeyUp(this->m_actions, EInputKeyType::INPUT_TYPE_MOUSE, key);

			// Call all the listeners
			for (auto& mouseListeners : this->m_mouseListeners) {
				mouseListeners->OnButtonPressed(ascii);
			}
		}

		void CInputDevice::SetMouseKeyDown(const EInputKey key, const unsigned int ascii) {
			LOG_TRACE;

			SetKeyDown(this->m_actions, EInputKeyType::INPUT_TYPE_MOUSE, key);
		}

		void CInputDevice::SetGamepadKey(const unsigned int gamepadId, EInputKey key, const float factor) {
			LOG_TRACE;
			Assert(gamepadId >= 0 && gamepadId < this->m_gamepadActions.size());

			if (factor == 0) {
				SetKeyUp(this->m_gamepadActions[gamepadId], EInputKeyType::INPUT_TYPE_GAMEPAD, key);
			}
			else {
				SetKeyDown(this->m_gamepadActions[gamepadId], EInputKeyType::INPUT_TYPE_GAMEPAD, key, factor);
			}
		}

		void CInputDevice::ResetMouseMovement() {
			LOG_TRACE;
			this->m_mouseMovement = CVector2I(0, 0);
		}

		void CInputDevice::FreezeAction(const unsigned int action) {
			if (this->m_actions.find(action) != this->m_actions.end() && this->m_actions[action].state == EInputKeyState::INPUT_KEY_STATE_DOWN) {
				this->m_actions[action].state = EInputKeyState::INPUT_KEY_STATE_FROZEN;
			}
		}

		void CInputDevice::FreezeGamepadAction(const unsigned int gamepadId, const unsigned int action) {
			if (this->m_gamepadActions[gamepadId].find(action) != this->m_gamepadActions[gamepadId].end() && this->m_gamepadActions[gamepadId][action].state == EInputKeyState::INPUT_KEY_STATE_DOWN) {
				this->m_gamepadActions[gamepadId][action].state = EInputKeyState::INPUT_KEY_STATE_FROZEN;
			}
		}

		bool CInputDevice::IsActionActive(const unsigned int action) const {
			LOG_TRACE;
			Assert(this->m_actions.find(action) != this->m_actions.end());

			if (this->m_actions.find(action) != this->m_actions.end()) {
				return this->m_actions.at(action).state == EInputKeyState::INPUT_KEY_STATE_DOWN;
			}
			return false;
		}

		bool CInputDevice::IsGamepadActionActive(const unsigned int gamepadId, const unsigned int action) const {
			LOG_TRACE;
			Assert(this->m_gamepadActions[gamepadId].find(action) != this->m_gamepadActions[gamepadId].end());

			return this->m_gamepadActions[gamepadId].at(action).state == EInputKeyState::INPUT_KEY_STATE_DOWN;
		}

		float CInputDevice::GetGamepadActionFactor(const unsigned int gamepadId, const unsigned int action) const {
			LOG_TRACE;
			Assert(this->m_gamepadActions[gamepadId].find(action) != this->m_gamepadActions[gamepadId].end());

			return this->m_gamepadActions[gamepadId].at(action).actionFactor;
		}

		const CVector2I& CInputDevice::GetMousePosition() const {
			LOG_TRACE;
			return this->m_mousePosition;
		}

		const CVector2I& CInputDevice::GetMouseMovement() const {
			LOG_TRACE;
			return this->m_mouseMovement;
		}

		void CInputDevice::SetKeyUp(std::map<unsigned int, SInputAction>& action, EInputKeyType type, EInputKey key) {
			LOG_TRACE;

			// Loop through the actions
			for (auto it = action.begin(); it != action.end(); ++it) {
				bool found = false;

				// Search if the action uses this key
				for (unsigned int i = 0; i < it->second.keys.size() && !found; ++i) {
					if (it->second.keys[i].type == type && it->second.keys[i].key == key)
						found = true;
				}

				// If the action uses this key, make the changes
				if (found) {
					// If the action state is DOWN
					if (it->second.state == EInputKeyState::INPUT_KEY_STATE_DOWN) {
						// Set it to the state UP
						it->second.state = EInputKeyState::INPUT_KEY_STATE_UP;
						it->second.actionFactor = 0;

						// Call all the listeners
						for (auto& keyboardListener : this->m_keyboardListeners) {
							keyboardListener->OnInputActionStopped(it->second.actionId);
						}

						// Check if it has a linked action to the state FROZEN
						if (it->second.linkedActionId != INPUT_NO_LINKED_ACTION && action[it->second.linkedActionId].state == EInputKeyState::INPUT_KEY_STATE_FROZEN) {
							// Change the state to DOWN
							action[it->second.linkedActionId].state = EInputKeyState::INPUT_KEY_STATE_DOWN;

							// Call all the listeners
							for (auto& keyboardListener : this->m_keyboardListeners) {
								keyboardListener->OnInputActionStarted(action[it->second.linkedActionId].actionId);
							}
						}
					}
					else {
						// If the action state is FROZEN
						if (it->second.state == EInputKeyState::INPUT_KEY_STATE_FROZEN) {
							// Set it to the state UP
							it->second.state = EInputKeyState::INPUT_KEY_STATE_UP;

							// Call all the listeners
							for (auto& keyboardListener : this->m_keyboardListeners) {
								keyboardListener->OnInputActionStopped(it->second.actionId);
							}
						}
					}
				}
			}
		}

		void CInputDevice::SetKeyDown(std::map<unsigned int, SInputAction>& action, const EInputKeyType type, EInputKey key, const float factor) {
			LOG_TRACE;

			// Loop through the actions
			for (auto it = action.begin(); it != action.end(); ++it) {
				bool found = false;

				// Search if the action uses this key
				for (unsigned int i = 0; i < it->second.keys.size() && !found; ++i) {
					if (it->second.keys[i].type == type && it->second.keys[i].key == key) {
						found = true;
					}
				}

				// If the action uses this key, make the changes
				if (found) {
					// If the action state is UP
					if (it->second.state == EInputKeyState::INPUT_KEY_STATE_UP) {
						// Set it to the state DOWN
						it->second.state = EInputKeyState::INPUT_KEY_STATE_DOWN;


						// Check if it has a linked action to the state DOWN
						if (it->second.linkedActionId != INPUT_NO_LINKED_ACTION && action[it->second.linkedActionId].state == EInputKeyState::INPUT_KEY_STATE_DOWN) {
							// Change the state to FROZEN
							action[it->second.linkedActionId].state = EInputKeyState::INPUT_KEY_STATE_FROZEN;

							// Call all the listeners
							for (auto& keyboardListener : this->m_keyboardListeners) {
								keyboardListener->OnInputActionStopped(action[it->second.linkedActionId].actionId);
							}
						}

						// Call all the listeners
						for (auto& keyboardListener : this->m_keyboardListeners) {
							keyboardListener->OnInputActionStarted(it->second.actionId);
						}
					}

					// Update the factor
					it->second.actionFactor = factor;
				}
			}
		}
	}
}
