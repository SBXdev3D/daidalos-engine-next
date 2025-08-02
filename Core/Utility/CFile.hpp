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

#include <string>

namespace daidalos {

	namespace core {

		/**
		* CFile class
		* File information class
		*/
		class CFile {
		private:
			std::string m_name;

		public:
			/**
			* Constructor
			*/
			CFile();

			/**
			* Constructor
			* @param name The full filename
			*/
			explicit CFile(const std::string& name);

			/**
			* Constructor
			* @param name The full filename
			*/
			explicit CFile(const char* name);

			/**
			* Check if a file exists
			* @return True if the file exists, false otherwise
			*/
			bool FileExists() const;

			/**
			* Get the file size (in Byte)
			* @return The file size in Byte
			*/
			int GetFileSize() const;

			/**
			* Get the full filename
			* @return The full filename
			*/
			const std::string& GetFullname() const;

			/**
			* Get the filename
			* @return The filename
			*/
			std::string GetFilename() const;

			/**
			* Get the short filename
			* @return The file full name
			*/
			std::string GetShortname() const;

			/**
			* Get the extension
			* @return The extension
			*/
			std::string GetExtension() const;
		};
	}
}

