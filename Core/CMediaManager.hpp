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
#include <set>
#include <memory>
#include <ranges>

#include "../Core/Utility/CFile.hpp"
#include "../Core/Logger/CScopeTracer.hpp"
#include "../Core/Memory/CExceptionLoad.hpp"
#include "../Core/CLoaderExtensions.hpp"

namespace daidalos {

	namespace core {

		/**
		* Media manager class
		* Handle the  process of loading and saving medias by using the correct loader
		*/
		class CMediaManager {

		private:
			std::set<std::string> m_paths;

		public:
			/**
			* Add a loader in the loaders list
			* @param loader A loader pointer
			* @param extensions A list of extensions supported by the loader, separated bu a space character
			*/
			template <class T> void AddLoader(ILoader<T>* loader, const std::string& extensions);

			/**
			* Unload a type of loader
			*/
			template <class T> void RemoveLoader();

			/**
			* Save a media into a file
			* @param object The object to save
			* @param filename The filename
			*/
			template <class T> void SaveMediaToFile(T* object, const std::string& filename);

			/**
			* Load a media from a file
			* Find the corresponding loader and load the file if a compatible loader is found
			* @param filename The filename
			* @return The media
			*/
			template <class T> T* LoadMediaFromFile(const std::string& filename);

			/**
			* Add a search path for the medias.
			* @param path The path to add.
			*/
			void AddSearchPath(const std::string& path);

		private:
			/**
			* Find a media in the paths.
			* @param filename The file fname.
			* @return The file fullname.
			*/
			CFile FindMedia(const std::string& filename);

			/**
			* Constructor
			*/
			CMediaManager();

		public:
			CMediaManager(const CMediaManager& copy) = delete;
			const CMediaManager& operator=(const CMediaManager& copy) = delete;
		};

		template <class T> void CMediaManager::AddLoader(ILoader<T>* loader, const std::string& extensions) {

			auto tokens = extensions | std::ranges::views::split(std::string(" "));
			auto loaderPtr = std::shared_ptr<>(loader);

			std::set<ILoader<T>*> replacedLoaders;
			for (const auto& extension : tokens) {

				// Store the loaders that are replaced
				if (CLoaderExtensions<T>::s_loaders.find(extension) != CLoaderExtensions<T>::s_loaders.end()) {
					replacedLoaders.insert(CLoaderExtensions<T>::s_loaders[extension]);
				}

				// Set or replace
				CLoaderExtensions<T>::s_loaders[extension] = loaderPtr;
			}
		}

		template <class T> void CMediaManager::RemoveLoader() {

			// Delete all the loaders
			CLoaderExtensions<T>::s_loaders.clear();
		}

		template <class T> void CMediaManager::SaveMediaToFile(T* object, const std::string& filename) {
			const std::string extension = CFile(filename).GetExtension();
			if (CLoaderExtensions<T>::IsExtensionFound(extension)) {
				CLoaderExtensions<T>::s_loaders[extension]->SaveToFile(object, filename);
			}
		}

		template <class T> T* CMediaManager::LoadMediaFromFile(const std::string& filename) /* throw(CExceptionLoad) */ {
			// Search the full path for the media
			CFile mediaPath = FindMedia(filename);

			// Find the loader to use to load the media
			const std::string extension = CFile(filename).GetExtension();
			if (CLoaderExtensions<T>::IsExtensionFound(extension)) {
				return CLoaderExtensions<T>::s_loaders[extension]->LoadFromFile(mediaPath.GetFullname());
			}
			else {
				spdlog::error("Unable to load the file {}", filename);

				throw new CExceptionLoad(filename);
			}
		}
	}
}

