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
#include <fstream>
#include "../../Core/Logger/CScopeTracer.hpp"
#include "../../Core/Utility/CFile.hpp"

namespace daidalos {

	namespace core {

		/**
		* Constructor
		*/
		CFile::CFile() {
			LOG_TRACE;
		}

		/**
		* Constructor
		* @param name The full filename
		*/
		CFile::CFile(const std::string& name) {

			LOG_TRACE;
			this->m_name = name;
			std::replace(this->m_name.begin(), this->m_name.end(), '/', '\\');
		}

		/**
		* Constructor
		* @param name The full filename
		*/
		CFile::CFile(const char* name) {
			LOG_TRACE;

			this->m_name = std::string(name);
			std::replace(this->m_name.begin(), this->m_name.end(), '/', '\\');
		}

		/**
		* Check if a file exists
		* @return True if the file exists, false otherwise
		*/
		bool CFile::FileExists() const {
			LOG_TRACE;

			bool result = false;
			std::ifstream file(this->m_name.c_str());

			if (file.is_open()) {
				result = true;
				file.close();
			}

			return result;
		}

		/**
		* Get the file size (in Byte)
		* @return The file size in Byte
		*/
		int CFile::GetFileSize() const {
			LOG_TRACE;

			int size = 0;
			std::ifstream file(this->m_name.c_str());
			if (file.is_open()) {
				file.seekg(0, std::ios::end);
				size = static_cast<int>(file.tellg()) * sizeof(char);
				file.close();
			}
			return size;
		}

		/**
		* Get the full filename
		* @return The full filename
		*/
		const std::string& CFile::GetFullname() const {
			LOG_TRACE;
			return this->m_name;
		}

		/**
		* Get the filename
		* @return The filename
		*/
		std::string CFile::GetFilename() const {
			LOG_TRACE;

			const auto position = this->m_name.find_last_of("\\/");

			if (position != std::string::npos) {
				return this->m_name.substr(position + 1, std::string::npos);
			} else {
				return this->m_name;
			}
		}

		/**
		* Get the short filename
		* @return The file full name
		*/
		std::string CFile::GetShortname() const {
			LOG_TRACE;
			return GetFilename().substr(0, GetFilename().find_last_of('.'));
		}

		/**
		* Get the extension
		* @return The extension
		*/
		std::string CFile::GetExtension() const {
			LOG_TRACE;

			const auto position = this->m_name.find_last_of('.');
			if (position != std::string::npos) {
				return this->m_name.substr(position + 1, std::string::npos);
			} else {
				return "";
			}
		}
	}
}
