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
#include "../Core/Utility/CFile.hpp"

namespace daidalos {

	namespace core {

		/**
	* Basic class used for every resources
	*/
		class IResource {
		private:
			static int s_resourceCounter;

		private:
			int m_referenceCount;
			CFile m_name;

		public:
			/**
			* Constructor
			*/
			IResource();

			/**
			* Constructor
			* @param name The resource name
			*/
			explicit IResource(const std::string& name);

			/**
			* Destructor
			*/
			virtual ~IResource();

			/**
			* Get the ressource name
			* @return The name
			*/
			std::string GetName() const;

			/**
			* Remove a reference and delete the ressource if not used anymore
			* @return The reference count
			*/
			int Release();

			/**
			* Set the name
			* @param name The name
			*/
			void SetName(const std::string& name);

			/**
			* Add a reference on the ressource
			*/
			void AddReference();

			/**
			* Called when the device is lost
			*/
			virtual void OnDeviceLost();

			/**
			* Called when the device is reset
			*/
			virtual void OnDeviceReset();

		public:
			IResource(const IResource& resource) = delete;
			const IResource& operator=(const IResource& copy) = delete;
		};
	}
}

