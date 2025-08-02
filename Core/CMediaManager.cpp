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

#include "../Core/CMediaManager.hpp"

namespace daidalos {

	namespace core {

		CMediaManager::CMediaManager() = default;

		void CMediaManager::AddSearchPath(const std::string& path) {
			LOG_TRACE;
			this->m_paths.insert(path);
		}

		CFile CMediaManager::FindMedia(const std::string& filename) {
			LOG_TRACE;

			for (auto& it : this->m_paths) {
				CFile file = CFile(it + filename);
				if (file.FileExists()) {
					return file;
				}
			}

			spdlog::error("Unable to locate the file {}", filename);

			throw CExceptionLoad(filename);
		}
	}
}
