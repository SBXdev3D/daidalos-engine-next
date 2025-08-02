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

#include "../../Core/Memory/CException.hpp"

namespace daidalos {

	namespace core {

		/**
		* Constructor
		*/
		CException::CException() = default;

		/**
		* Destructor
		*/
		CException::~CException() = default;

		/**
		* Set the message
		* @param message The message
		*/
		void CException::SetMessage(const std::string& message) {
			this->m_message = message;
		}

		/**
		* Get the exception message
		* @return The exception message
		*/
		const char* CException::what() const throw() {
			return this->m_message.c_str();
		}
	}
}
