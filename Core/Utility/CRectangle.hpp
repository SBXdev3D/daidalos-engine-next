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

#include "../../Core/Utility/CVector2.hpp"

namespace daidalos {

	namespace core {

		enum class ERectangleIntersection {
			RECTANGLE_INTERSECTION_OUTSIDE,
			RECTANGLE_INTERSECTION_INSIDE,
			RECTANGLE_INTERSECTION_INTERSECT
		};

		/**
		* Rectangle template class
		* Handle a rectangle
		*/
		template <class T> class CRectangle {
		private:
			CVector2<T> m_topLeft;
			CVector2<T> m_bottomRight;

		public:
			CRectangle(const CVector2<T>& origin = CVector2<T>(0, 0), const CVector2<T>& size = CVector2<T>(0, 0));
			CRectangle(const T x, const T y, const T width, const T height);
			const T GetTop() const;
			const T GetLeft() const;
			const T GetBottom() const;
			const T GetRight() const;
			const CVector2<T> GetSize() const;
			const T GetWidth() const;
			const T GetHeight() const;
			const CRectangle<T> operator+(const CVector2<T>& value) const;
			const CRectangle<T> operator-(const CVector2<T>& value) const;
			const CRectangle<T>& operator+=(const CVector2<T>& value);
			const CRectangle<T>& operator-=(const CVector2<T>& value);
			const CRectangle<T>& operator*=(const T number);
			const CRectangle<T>& operator/=(const T number);
			bool operator==(const CRectangle<T>& rect) const;
			bool operator!=(const CRectangle<T>& rect) const;
			ERectangleIntersection Intersect(const CVector2<T>& point) const;
			ERectangleIntersection Intersect(const CRectangle<T>& rect) const;
		};

		template <class T> const CRectangle<T> operator+(const CRectangle<T>& vector, const CVector2<T>& value);
		template <class T> const CRectangle<T> operator+(const CVector2<T>& value, const CRectangle<T>& vector);
		template <class T> const CRectangle<T> operator-(const CRectangle<T>& vector, const CVector2<T>& value);
		template <class T> const CRectangle<T> operator-(const CVector2<T>& value, const CRectangle<T>& vector);
		template <class T> const CRectangle<T> operator*(const CRectangle<T>& vector, const T number);
		template <class T> const CRectangle<T> operator*(const T number, const CRectangle<T>& vector);
		template <class T> const CRectangle<T> operator/(const CRectangle<T>& vector, const T number);
		template <class T> const CRectangle<T> operator/(const T number, const CRectangle<T>& vector);

		/**
		* Constructor
		* @param origin The top-left point
		* @param size The rectangle size
		*/
		template <class T> inline CRectangle<T>::CRectangle(const CVector2<T>& origin, const CVector2<T>& size) {
			this->m_topLeft = origin;
			this->m_bottomRight = origin + size;
		}

		/**
		* Constructor
		* @param x The left coordinate
		* @param y The top coordinate
		* @param width The width
		* @param height The height
		*/
		template <class T> inline CRectangle<T>::CRectangle(const T x, const T y, const T width, const T height) {
			this->m_topLeft.x = x;
			this->m_topLeft.y = y;
			this->m_bottomRight.x = x + width;
			this->m_bottomRight.y = y + height;
		}

		/**
		* Get the top coordinate
		* @return The top coordinate
		*/
		template <class T> inline const T CRectangle<T>::GetTop() const {
			return this->m_topLeft.y;
		}

		/**
		* Get the left coordinate
		* @return The left coordinate
		*/
		template <class T> inline const T CRectangle<T>::GetLeft() const {
			return this->m_topLeft.x;
		}

		/**
		* Get the bottom coordinate
		* @return The bottom coordinate
		*/
		template <class T> inline const T CRectangle<T>::GetBottom() const {
			return this->m_bottomRight.y;
		}

		/**
		* Get the right coordinate
		* @return The right coordinate
		*/
		template <class T> inline const T CRectangle<T>::GetRight() const {
			return this->m_bottomRight.x;
		}

		/**
		* Get the size
		* @return The size
		*/
		template <class T> inline const CVector2<T> CRectangle<T>::GetSize() const {
			return CVector2<T>(GetWidth(), GetHeight());
		}

		/**
		* Get the width
		* @return The width
		*/
		template <class T> inline const T CRectangle<T>::GetWidth() const {
			return this->m_bottomRight.x - this->m_topLeft.x;
		}

		/**
		* Get the height
		* @return The height
		*/
		template <class T> inline const T CRectangle<T>::GetHeight() const {
			return this->m_bottomRight.y - this->m_topLeft.y;
		}

		/**
		* + operator override
		* @param value CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> CRectangle<T>::operator+(const CVector2<T>& value) const {
			CRectangle<T> tmp(*this);
			tmp.m_bottomRight += value;
			tmp.m_topLeft += value;
			return tmp;
		}

		/**
		* - operator override
		* @param value CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> CRectangle<T>::operator-(const CVector2<T>& value) const {
			CRectangle<T> tmp(*this);
			tmp.m_bottomRight -= value;
			tmp.m_topLeft -= value;
			return tmp;
		}

		/**
		* += operator override
		* @param value A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T>& CRectangle<T>::operator+=(const CVector2<T>& value) {
			this->m_bottomRight += value;
			this->m_topLeft += value;
			return *this;
		}

		/**
		* -= operator override
		* @param value A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T>& CRectangle<T>::operator-=(const CVector2<T>& value) {
			this->m_bottomRight -= value;
			this->m_topLeft -= value;
			return *this;
		}

		/**
		* *= operator override
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T>& CRectangle<T>::operator*=(const T number) {
			this->m_bottomRight *= number;
			this->m_topLeft *= number;
			return *this;
		}

		/**
		* *= operator override
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T>& CRectangle<T>::operator/=(const T number) {
			this->m_bottomRight /= number;
			this->m_topLeft /= number;
			return *this;
		}

		/**
		* == operator override
		* @param rect A CRectangle
		* @return The equal operation result
		*/
		template <class T> inline bool CRectangle<T>::operator==(const CRectangle<T>& rect) const {
			return this->m_topLeft == rect.m_topLeft && this->m_bottomRight == rect.m_bottomRight;
		}

		/**
		* != operator override
		* @param rect A CRectangle
		* @return The non equal operation result
		*/
		template <class T> inline bool CRectangle<T>::operator!=(const CRectangle<T>& rect) const {
			return !(*this == rect);
		}

		/**
		* Test the intersection with a point
		* @param point The point coordinates
		* @return The intersection result as a ERectangeIntersection
		*/
		template <class T> inline ERectangleIntersection CRectangle<T>::Intersect(const CVector2<T>& point) const {
			if (point.x >= this->m_topLeft.x && point.x <= this->m_bottomRight.x && point.y >= this->m_topLeft.y && point.y <= this->m_bottomRight.y)
				return RECTANGLE_INTERSECTION_INSIDE;
			else
				return RECTANGLE_INTERSECTION_OUTSIDE;
		}

		/**
		* Test the intersection with a rectangle
		* @param rect The rectangle
		* @return The intersection result as a ERectangeIntersection
		*/
		template <class T> inline ERectangleIntersection CRectangle<T>::Intersect(const CRectangle<T>& rect) const {
			CVector2<T> start = CVector2<T>(std::max(this->m_topLeft.x, rect.m_topLeft.x), std::max(this->m_topLeft.y, rect.m_topLeft.y));
			CVector2<T> end = CVector2<T>(std::min(this->m_bottomRight.x, rect.m_bottomRight.x), std::min(this->m_bottomRight.y, rect.m_bottomRight.y));
			CRectangle<T> tmp = CRectangle<T>(start, end - start);

			if (start.x > end.x || start.y > end.y)
				return RECTANGLE_INTERSECTION_OUTSIDE;
			else
				if (tmp == *this || tmp == rect)
					return RECTANGLE_INTERSECTION_INSIDE;
				else
					return RECTANGLE_INTERSECTION_INTERSECT;
		}

		/**
		* + operator override
		* @param rectangle A CRectangle
		* @param value A CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator+(const CRectangle<T>& rectangle, const CVector2<T>& value) {
			CRectangle<T> tmp(rectangle);
			tmp += value;
			return tmp;
		}

		/**
		* + operator override
		* @param rectangle A CRectangle
		* @param value A CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator+(const CVector2<T>& value, const CRectangle<T>& rectangle) {
			return rectangle + value;
		}

		/**
		* - operator override
		* @param rectangle A CRectangle
		* @param value A CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator-(const CRectangle<T>& rectangle, const CVector2<T>& value) {
			CRectangle<T> tmp(rectangle);
			tmp -= value;
			return tmp;
		}

		/**
		* - operator override
		* @param rectangle A CRectangle
		* @param value A CVector2
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator-(const CVector2<T>& value, const CRectangle<T>& rectangle) {
			return rectangle - value;
		}

		/**
		* * operator override
		* @param rectangle A CRectangle
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator*(const CRectangle<T>& rectangle, const T number) {
			CRectangle<T> tmp(rectangle);
			tmp *= number;
			return tmp;
		}

		/**
		* * operator override
		* @param rectangle A CRectangle
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator*(const T number, const CRectangle<T>& rectangle) {
			return rectangle * number;
		}

		/**
		* / operator override
		* @param rectangle A CRectangle
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator/(const CRectangle<T>& rectangle, const T number) {
			CRectangle<T> tmp(rectangle);
			tmp /= number;
			return tmp;
		}

		/**
		* / operator override
		* @param rectangle A CRectangle
		* @param number A number
		* @return The CRectangle
		*/
		template <class T> inline const CRectangle<T> operator/(const T number, const CRectangle<T>& rectangle) {
			return rectangle / number;
		}

		typedef CRectangle<int> CRectangleI;
		typedef CRectangle<float> CRectangleF;
	}

}
