/*
	Copyright © 2018-2020, Morgan Grundy

	This file is part of Mosaic Magnifique.

    Mosaic Magnifique is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Mosaic Magnifique is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Mosaic Magnifique.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef GRIDBOUNDS_H
#define GRIDBOUNDS_H

#include <opencv2/core.hpp>

class GridBounds
{
public:
    GridBounds();

    void addBound(const cv::Rect &t_bound);
    void addBound(const int t_height, const int t_width);

    void mergeBounds();

    void clear();

    std::vector<cv::Rect>::const_iterator cbegin() const;
    std::vector<cv::Rect>::const_iterator cend() const;

    bool empty() const;

private:
    std::vector<cv::Rect> bounds;
};

#endif // GRIDBOUNDS_H
