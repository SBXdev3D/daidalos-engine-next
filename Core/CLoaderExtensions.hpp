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

#include <map>
#include <memory>
#include "../Core/ILoader.hpp"

namespace daidalos {

	namespace core {

		/**
		* Handle all the medias loaders and check their extensions compatibilities
		*/
		template <class T> class CLoaderExtensions {
		public:
			static std::map < std::string, std::shared_ptr<ILoader<T>>> s_loaders;

			/**
			* Check if this loader type handle the given extension
			* @param extension The extension
			* @return True if the extension is found
			*/
			static bool IsExtensionFound(const std::string& extension) {
				bool ret = false;
				for (auto it = s_loaders.begin(); it != s_loaders.end() && !ret; ++it) {
					if (it->first == extension) {
						ret = true;
					}
				}
				return ret;
			}
		};

		template <class T> std::map<std::string, std::shared_ptr<ILoader<T>>> CLoaderExtensions<T>::s_loaders;
	}
}
