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

#include <unordered_map>
#include <memory>
#include <typeindex>
#include <type_traits>

namespace daidalos {

	namespace core {

        class EngineContext {
        public:
            template<typename T, typename... Args> void Register(Args&&... args) {
                std::type_index type = std::type_index(typeid(T));
                services[type] = std::make_shared<T>(std::forward<Args>(args)...);
            }

            template<typename T> T& Get() {
                std::type_index type = std::type_index(typeid(T));
                return *std::static_pointer_cast<T>(services.at(type));
            }

        private:
            std::unordered_map<std::type_index, std::shared_ptr<void>> services;
        };
	}
}
