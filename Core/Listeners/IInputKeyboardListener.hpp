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

#pragma once

namespace daidalos {

	namespace core {

		/**
		* Keyboard inputs listener interface.
		*/
		class IInputKeyboardListener {
		public:
			/**
			* Destructor
			*/
			virtual ~IInputKeyboardListener() {}

			/**
			* Called when a keyboard action is started
			* @param actionId The action id
			*/
			virtual void OnInputActionStarted(unsigned int actionId) = 0;

			/**
			* Called when a keyboard action is stopped
			* @param actionId The action id
			*/
			virtual void OnInputActionStopped(unsigned int actionId) = 0;

			/**
			* Called when a key has been pressed
			* @param key The key
			*/
			virtual void OnKeyPressed(unsigned int key) = 0;
		};
	}
}
