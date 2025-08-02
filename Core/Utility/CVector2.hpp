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

#include <cmath>
#include <string>
#include <ranges>
#include "../../Core/Utility/MathConstants.hpp"

namespace daidalos {

	namespace core {

		/**
		* CVector2 class
		* Basic math class for vectors of size 2
		*/
		template <class T> class CVector2 {
		public:
			T x;
			T y;

			CVector2();
			CVector2(const CVector2<T> & vec);
			CVector2(const T x, const T y);
			CVector2(const T * const f);
			const CVector2<T> operator+(const CVector2<T> & vec) const;
			const CVector2<T> operator-(const CVector2<T> & vec) const;
			const CVector2<T> operator*(const CVector2<T> & vec) const;
			const CVector2<T> operator/(const CVector2<T> & vec) const;
			const CVector2<T> operator-() const;
			const CVector2<T> Normalize() const;
			CVector2<T> & operator=(const CVector2<T> & vec);
			const CVector2<T> & operator+=(const CVector2<T> & vec);
			const CVector2<T> & operator-=(const CVector2<T> & vec);
			const CVector2<T> & operator*=(const CVector2<T> & vec);
			const CVector2<T> & operator*=(const T number);
			const CVector2<T> & operator/=(const CVector2<T> & vec);
			const CVector2<T> & operator/=(const T number);
			bool operator==(const CVector2<T> & vec) const;
			bool operator!=(const CVector2<T> & vec) const;
			const T Length() const;
			const T CrossProduct(const CVector2<T> & vec) const;
			const T DotProduct(const CVector2<T> & vec) const;
			const T Distance(const CVector2<T> & vec) const;
			const std::string ToString(const std::string separator = ":") const;
			operator T * ();
			operator const T * () const;
			static const CVector2<T> GetFrom(const std::string text, const CVector2<T> & defaultValue = CVector2<T>(), const std::string separator = ":");
			static const T Length(const CVector2<T> & vec);
			static const T CrossProduct(const CVector2<T> & vec1, const CVector2<T> & vec2);
			static const T DotProduct(const CVector2<T> & vec1, const CVector2<T> & vec2);
			static const T Distance(const CVector2<T> & vec1, const CVector2<T> & vec2);
			static const CVector2<T> Normalize(CVector2<T> & vec);
		};

		template <class T> const CVector2<T> operator*(const CVector2<T> & vec, const T number);
		template <class T> const CVector2<T> operator*(const T number, const CVector2<T> & vec);
		template <class T> const CVector2<T> operator/(const CVector2<T> & vec, const T number);
		template <class T> const CVector2<T> operator/(const T number, const CVector2<T> & vec);

		/**
		* Constructor
		*/
		template <class T> inline CVector2<T>::CVector2() {
			this->x = 0;
			this->y = 0;
		}

		/**
		* Constructor
		* @param vec A CVector2
		*/
		template <class T> inline CVector2<T>::CVector2(const CVector2<T> & vec) {
			this->x = vec.x;
			this->y = vec.y;
		}

		/**
		* Constructor
		* @param x The x coordinate
		* @param y The y coordinate
		*/
		template <class T> inline CVector2<T>::CVector2(const T x, const T y) {
			this->x = x;
			this->y = y;
		}

		/**
		* Constructor
		* @param f The coordinate
		*/
		template <class T> inline CVector2<T>::CVector2(const T * const f) {
			this->x = f[0];
			this->y = f[1];
		}

		/**
		* + operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::operator+(const CVector2<T> & vec) const {
			CVector2<T> tmp(*this);
			tmp.x += vec.x;
			tmp.y += vec.y;
			return tmp;
		}

		/**
		* - operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::operator-(const CVector2<T> & vec) const {
			CVector2<T> tmp(*this);
			tmp.x -= vec.x;
			tmp.y -= vec.y;
			return tmp;
		}

		/**
		* * operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::operator*(const CVector2<T> & vec) const {
			CVector2<T> tmp(*this);
			tmp.x *= vec.x;
			tmp.y *= vec.y;
			return tmp;
		}

		/**
		* / operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::operator/(const CVector2<T> & vec) const {
			CVector2<T> tmp(*this);
			tmp.x /= vec.x;
			tmp.y /= vec.y;
			return tmp;
		}

		/**
		* - operator override
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::operator-() const {
			CVector2<T> tmp(*this);
			tmp.x *= -1;
			tmp.y *= -1;
			return tmp;
		}

		/**
		* Normalize the vector CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::Normalize() const {
			CVector2<T> tmp = *this;
			T length = Length();

			if(length > 0)
				tmp = CVector2(this->x / length, this->y / length);

			return tmp;
		}

		/**
		* = operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline CVector2<T> & CVector2<T>::operator=(const CVector2<T> & vec) {
			this->x = vec.x;
			this->y = vec.y;
			return *this;
		}

		/**
		* += operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator+=(const CVector2<T> & vec) {
			this->x += vec.x;
			this->y += vec.y;
			return *this;
		}

		/**
		* -= operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator-=(const CVector2<T> & vec) {
			this->x -= vec.x;
			this->y -= vec.y;
			return *this;
		}

		/**
		* *= operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator*=(const CVector2<T> & vec) {
			this->x *= vec.x;
			this->y *= vec.y;
			return *this;
		}

		/**
		* *= operator override
		* @param number A number
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator*=(const T number) {
			*this = *this * number;
			return *this;
		}

		/**
		* /= operator override
		* @param vec CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator/=(const CVector2<T> & vec) {
			this->x /= vec.x;
			this->y /= vec.y;
			return *this;
		}

		/**
		* /= operator override
		* @param number A number
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> & CVector2<T>::operator/=(const T number) {
			this->x /= number;
			this->y /= number;
			return *this;
		}

		/**
		* == operator override
		* @param vec CVector2
		* @return The equal result
		*/
		template <class T> inline bool CVector2<T>::operator==(const CVector2<T> & vec) const {
			return std::abs(this->x - vec.x) < Epsilon && std::abs(this->y - vec.y) < Epsilon;
		}

		/**
		* != operator override
		* @param vec CVector2
		* @return The !equal result
		*/
		template <class T> inline bool CVector2<T>::operator!=(const CVector2<T> & vec) const {
			return !(*this == vec);
		}

		/**
		* Get the length
		* @return The length
		*/
		template <class T> inline const T CVector2<T>::Length() const {
			return sqrt(this->x * this->x + this->y * this->y);
		}

		/**
		* Perform a cross product
		* @param vec A CVector2
		* @return The cross product
		*/
		template <class T> inline const T CVector2<T>::CrossProduct(const CVector2<T> & vec) const {
			return this->x * vec.y - this->y * vec.x;
		}

		/**
		* Perform a dot product
		* @param vec A CVector2
		* @return The dot product
		*/
		template <class T> inline const T CVector2<T>::DotProduct(const CVector2<T> & vec) const {
			return this->x * vec.x + this->y * vec.y;
		}

		/**
		* Perform a distance calculation
		* @param vec A CVector2
		* @return The distance
		*/
		template <class T> inline const T CVector2<T>::Distance(const CVector2<T> & vec) const {
			return CVector2<T>(vec.x - this->x, vec.y - this->y).Length();
		}

		/**
		* Get a string from a CVector2
		* @param separator The separator
		* @return A string
		*/
		template <class T> inline const std::string CVector2<T>::ToString(const std::string separator) const {
			std::stringstream ss;

			ss << this->x << separator << this->y;
			return ss.str();
		}

		/**
		* T * cast operator override
		* @return A float pointer
		*/
		template <class T> inline CVector2<T>::operator T * () {
			return &this->x;
		}

		/**
		* T * cast operator override
		* @return A float pointer
		*/
		template <class T> inline CVector2<T>::operator const T * () const {
			return &this->x;
		}

		/**
		* Get a CVector2 from a string
		* @param text The text
		* @param defaultValue The default value
		* @param separator The separator
		* @return The vector
		*/
		template <class T> inline const CVector2<T> CVector2<T>::GetFrom(const std::string text, const CVector2<T> & defaultValue, const std::string separator) {
			CVector2<T> result;
			auto tokens = text | std::ranges::views::split(separator);

			if(tokens.size == 2) {
				std::istringstream ss;

				ss.clear();
				ss.str(std::string(tokens[0].begin(), tokens[0].end()));
				ss >> result.x;

				ss.clear();
				ss.str(std::string(tokens[1].begin(), tokens[1].end()));
				ss >> result.y;
			} else {
				result = defaultValue;
			}

			return result;
		}

		/**
		* Get the length of a CVector2
		* @param vec A CVector2
		* @return The length
		*/
		template <class T> inline const T CVector2<T>::Length(const CVector2<T> & vec) {
			return vec.Length();
		}

		/**
		* Perform a cross product
		* @param vec1 A CVector2
		* @param vec2 A CVector2
		* @return The cross product
		*/
		template <class T> inline const T CVector2<T>::CrossProduct(const CVector2<T> & vec1, const CVector2<T> & vec2) {
			return vec1.CrossProduct(vec2);
		}

		/**
		* Perform a dot product
		* @param vec1 A CVector2
		* @param vec2 A CVector2
		* @return The dot product
		*/
		template <class T> inline const T CVector2<T>::DotProduct(const CVector2<T> & vec1, const CVector2<T> & vec2) {
			return vec1.DotProduct(vec2);
		}

		/**
		* Perform a distance calculation
		* @param vec1 A CVector2
		* @param vec2 A CVector2
		* @return The distance
		*/
		template <class T> inline const T CVector2<T>::Distance(const CVector2<T> & vec1, const CVector2<T> & vec2) {
			return vec1.Distance(vec2);
		}

		/**
		* Normalize a CVector2
		* @param vec A CVector2
		*/
		template <class T> inline const CVector2<T> CVector2<T>::Normalize(CVector2<T> & vec) {
			return vec.Normalize();
		}

		/**
		* * operator override
		* @param vec A CVector2
		* @param number A number
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> operator*(const CVector2<T> & vec, const T number) {
			CVector2<T> tmp(vec);
			tmp.x *= number;
			tmp.y *= number;
			return tmp;
		}

		/**
		* * operator override
		* @param number A number
		* @param vec A CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> operator*(const T number, const CVector2<T> & vec) {
			return vec * number;
		}

		/**
		* / operator override
		* @param vec A CVector2
		* @param number A number
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> operator/(const CVector2<T> & vec, const T number) {
			CVector2<T> tmp(vec);
			tmp.x /= number;
			tmp.y /= number;
			return tmp;
		}

		/**
		* / operator override
		* @param number A number
		* @param vec A CVector2
		* @return The CVector2
		*/
		template <class T> inline const CVector2<T> operator/(const T number, const CVector2<T> & vec) {
			return CVector2<T>(number / vec.x, number / vec.y);
		}

		typedef CVector2<int> CVector2I;
		typedef CVector2<float> CVector2F;
	}
}
