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

#include <sstream>
#include "../../Core/Memory/CExceptionDelete.hpp"

namespace daidalos {

	namespace core {

		/**
		* Constructor
		* @param ptr The memory address
		* @param filename The exception owner file
		* @param line The error line
		* @param isArray if the memory block is an array
		*/
		CExceptionDelete::CExceptionDelete(void* ptr, const std::string& filename, const int line, const bool isArray) {
			std::ostringstream stream;
			stream << "Bad delete operation" << std::endl << "Adress: 0x" << ptr << std::endl << "File: " << filename << std::endl << "Line: " << line << std::endl << "Array: ";
			if (isArray)
				stream << "yes";
			else
				stream << "no";
			stream << "." << std::endl;

			SetMessage(stream.str());
		}
	}
}
