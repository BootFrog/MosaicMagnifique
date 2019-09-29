#include "photomosaicgenerator.h"

#define _USE_MATH_DEFINES

#include <vector>
#include <cmath>
#include <climits>
#include <algorithm>
#include <opencv2/core/mat.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <QDebug>

PhotomosaicGenerator::PhotomosaicGenerator(QWidget *t_parent)
    : QProgressDialog{t_parent}, m_mode{Mode::RGB_EUCLIDEAN}, m_repeatRange{0}, m_repeatAddition{0}
{
    setWindowModality(Qt::WindowModal);
}

PhotomosaicGenerator::~PhotomosaicGenerator() {}

//Returns a Photomosaic of the main image made of the library images
cv::Mat PhotomosaicGenerator::generate(cv::Mat &mainImage, const std::vector<cv::Mat> &library)
{
    //If using a CIE mode converts all images to Lab colour space
    if (m_mode == Mode::CIE76 || m_mode == Mode::CIEDE2000)
    {
        setMaximum(static_cast<int>(library.size()) + 1);
        setValue(0);
        setLabelText("Converting images to LAB colour space...");

        cv::cvtColor(mainImage, mainImage, cv::COLOR_BGR2Lab);
        setValue(value() + 1);

        for (auto image: library)
        {
            cv::cvtColor(image, image, cv::COLOR_BGR2Lab);
            setValue(value() + 1);
        }
    }

    const cv::Point cellSize(library.front().cols, library.front().rows);
    const cv::Point gridSize(mainImage.cols / cellSize.x, mainImage.rows / cellSize.y);

    setMaximum(gridSize.x * gridSize.y);
    setValue(0);
    setLabelText("Finding best fits...");

    //Split main image into grid
    //Find best match for each cell in grid
    std::vector<std::vector<cv::Mat>> result(static_cast<size_t>(gridSize.x),
                                             std::vector<cv::Mat>(static_cast<size_t>(gridSize.y)));
    std::vector<std::vector<size_t>> grid(static_cast<size_t>(gridSize.x),
                                             std::vector<size_t>(static_cast<size_t>(gridSize.y), 0));
    for (int x = 0; x < gridSize.x; ++x)
    {
        for (int y = 0; y < gridSize.y; ++y)
        {
            const cv::Point cellStart(x * cellSize.x, y * cellSize.y);
            const cv::Point cellEnd((x+1) * cellSize.x, (y+1) * cellSize.y);

            const cv::Mat cell = mainImage(cv::Range(cellStart.y, cellEnd.y),
                                     cv::Range(cellStart.x, cellEnd.x));

            //Calculate number of each library image in repeat range
            const std::map<size_t, int> repeats = calculateRepeats(grid, gridSize, x, y);

            int index;
            if (m_mode == Mode::CIEDE2000)
                index = findBestFitCIEDE2000(cell, library, repeats);
            else
                index = findBestFitEuclidean(cell, library, repeats);
            if (index < 0 || index >= static_cast<int>(library.size()))
            {
                qDebug() << "Failed to find a best fit";
                index = 0;
            }

            grid.at(static_cast<size_t>(x)).at(static_cast<size_t>(y)) = static_cast<size_t>(index);
            result.at(static_cast<size_t>(x)).at(static_cast<size_t>(y)) =
                    library.at(static_cast<size_t>(index));
            setValue(value() + 1);
            if (wasCanceled())
                return cv::Mat();
        }
    }

    //Combines all results into single image (mosaic)
    cv::Mat mosaic;
    std::vector<cv::Mat> mosaicRows(static_cast<size_t>(gridSize.x));
    for (size_t x = 0; x < static_cast<size_t>(gridSize.x); ++x)
        cv::vconcat(result.at(x), mosaicRows.at(x));
    cv::hconcat(mosaicRows, mosaic);

    if (m_mode == Mode::CIE76 || m_mode == Mode::CIEDE2000)
        cv::cvtColor(mosaic, mosaic, cv::COLOR_Lab2BGR);
    return mosaic;
}

//Compares pixels in the cell against the library images
//Returns the index of the library image with the smallest difference
//Used for mode RGB_EUCLIDEAN and CIE76
//(CIE76 is just a euclidean formulae in a different colour space)
int PhotomosaicGenerator::findBestFitEuclidean(const cv::Mat &cell,
                                               const std::vector<cv::Mat> &library,
                                               const std::map<size_t, int> &repeats) const
{
    int bestFit = -1;
    long double bestVariant = LDBL_MAX;

    for (size_t i = 0; i < library.size(); ++i)
    {
        const auto it = repeats.find(i);
        long double variant = (it != repeats.end()) ? it->second * m_repeatAddition : 0;

        //For cell and library image compare the corresponding pixels
        //Sum all pixel differences for total image difference
        const uchar *p_main, *p_im;
        for(int row = 0; row < cell.rows && variant < bestVariant; ++row)
        {
            p_main = cell.ptr<uchar>(row);
            p_im = library.at(i).ptr<uchar>(row);
            for (int col = 0; col < cell.cols * cell.channels() && variant < bestVariant;
                 ++col)
            {
                variant += static_cast<long double>(sqrt(pow(p_main[col] - p_im[col], 2) +
                                                         pow(p_main[col + 1] - p_im[col + 1], 2) +
                                                         pow(p_main[col + 2] - p_im[col + 2], 2)));
            }
        }

        //If image difference is less than current lowest then replace
        if (variant < bestVariant)
        {
            bestVariant = variant;
            bestFit = static_cast<int>(i);
        }
    }
    return bestFit;
}

//Compares pixels in the cell against the library images
//Returns the index of the library image with the smallest difference
//Used for mode CIEDE2000
int PhotomosaicGenerator::findBestFitCIEDE2000(const cv::Mat &cell,
                                               const std::vector<cv::Mat> &library,
                                               const std::map<size_t, int> &repeats) const
{
    int bestFit = -1;
    long double bestVariant = LDBL_MAX;

    for (size_t i = 0; i < library.size(); ++i)
    {
        const auto it = repeats.find(i);
        long double variant = (it != repeats.end()) ? it->second * m_repeatAddition : 0;

        //For cell and library image compare the corresponding pixels
        //Sum all pixel differences for total image difference
        const uchar *p_main, *p_im;
        for(int row = 0; row < cell.rows && variant < bestVariant; ++row)
        {
            p_main = cell.ptr<uchar>(row);
            p_im = library.at(i).ptr<uchar>(row);
            for (int col = 0; col < cell.cols * cell.channels() && variant < bestVariant;
                 ++col)
            {
                const double k_L = 1.0, k_C = 1.0, k_H = 1.0;
                const double deg360InRad = degToRad(360.0);
                const double deg180InRad = degToRad(180.0);
                const double pow25To7 = 6103515625.0; //pow(25, 7)

                const double C1 = sqrt((p_main[col + 1] * p_main[col + 1]) + (p_main[col + 2] *
                        p_main[col + 2]));
                const double C2 = sqrt((p_im[col + 1] * p_im[col + 1]) + (p_im[col + 2] * p_im[col + 2]));
                const double barC = (C1 + C2) / 2.0;

                const double G = 0.5 * (1 - sqrt(pow(barC, 7) / (pow(barC, 7) + pow25To7)));

                const double a1Prime = (1.0 + G) * p_main[col + 1];
                const double a2Prime = (1.0 + G) * p_im[col + 1];

                const double CPrime1 = sqrt((a1Prime * a1Prime) + (p_main[col + 2] * p_main[col + 2]));
                const double CPrime2 = sqrt((a2Prime * a2Prime) + (p_im[col + 2] * p_im[col + 2]));

                double hPrime1;
                if (p_main[col + 2] == 0 && a1Prime == 0.0)
                    hPrime1 = 0.0;
                else
                {
                    hPrime1 = atan2(p_main[col + 2], a1Prime);
                    //This must be converted to a hue angle in degrees between 0 and 360 by
                    //addition of 2 pi to negative hue angles.
                    if (hPrime1 < 0)
                        hPrime1 += deg360InRad;
                }

                double hPrime2;
                if (p_im[col + 2] == 0 && a2Prime == 0.0)
                    hPrime2 = 0.0;
                else
                {
                    hPrime2 = atan2(p_im[col + 2], a2Prime);
                    //This must be converted to a hue angle in degrees between 0 and 360 by
                    //addition of 2pi to negative hue angles.
                    if (hPrime2 < 0)
                        hPrime2 += deg360InRad;
                }

                const double deltaLPrime = p_im[col] - p_main[col];
                const double deltaCPrime = CPrime2 - CPrime1;

                double deltahPrime;
                const double CPrimeProduct = CPrime1 * CPrime2;
                if (CPrimeProduct == 0.0)
                    deltahPrime = 0;
                else
                {
                    //Avoid the fabs() call
                    deltahPrime = hPrime2 - hPrime1;
                    if (deltahPrime < -deg180InRad)
                        deltahPrime += deg360InRad;
                    else if (deltahPrime > deg180InRad)
                        deltahPrime -= deg360InRad;
                }

                const double deltaHPrime = 2.0 * sqrt(CPrimeProduct) * sin(deltahPrime / 2.0);

                const double barLPrime = (p_main[col] + p_im[col]) / 2.0;
                const double barCPrime = (CPrime1 + CPrime2) / 2.0;

                double barhPrime;
                const double hPrimeSum = hPrime1 + hPrime2;
                if (CPrime1 * CPrime2 == 0.0)
                    barhPrime = hPrimeSum;
                else
                {
                    if (fabs(hPrime1 - hPrime2) <= deg180InRad)
                        barhPrime = hPrimeSum / 2.0;
                    else
                    {
                        if (hPrimeSum < deg360InRad)
                            barhPrime = (hPrimeSum + deg360InRad) / 2.0;
                        else
                            barhPrime = (hPrimeSum - deg360InRad) / 2.0;
                    }
                }

                const double T = 1.0 - (0.17 * cos(barhPrime - degToRad(30.0))) +
                        (0.24 * cos(2.0 * barhPrime)) +
                        (0.32 * cos((3.0 * barhPrime) + degToRad(6.0))) -
                        (0.20 * cos((4.0 * barhPrime) - degToRad(63.0)));

                const double deltaTheta = degToRad(30.0) *
                        exp(-pow((barhPrime - degToRad(275.0)) / degToRad(25.0), 2.0));

                const double R_C = 2.0 * sqrt(pow(barCPrime, 7.0) / (pow(barCPrime, 7.0) + pow25To7));

                const double S_L = 1 +((0.015 * pow(barLPrime - 50.0, 2.0)) /
                                 sqrt(20 + pow(barLPrime - 50.0, 2.0)));
                const double S_C = 1 + (0.045 * barCPrime);
                const double S_H = 1 + (0.015 * barCPrime * T);

                const double R_T = (-sin(2.0 * deltaTheta)) * R_C;


                variant += static_cast<long double>(sqrt(pow(deltaLPrime / (k_L * S_L), 2.0) +
                                                         pow(deltaCPrime / (k_C * S_C), 2.0) +
                                                         pow(deltaHPrime / (k_H * S_H), 2.0) +
                                                         (R_T * (deltaCPrime / (k_C * S_C)) *
                                                          (deltaHPrime / (k_H * S_H)))));

            }
        }

        //If image difference is less than current lowest then replace
        if (variant < bestVariant)
        {
            bestVariant = variant;
            bestFit = static_cast<int>(i);
        }
    }
    return bestFit;
}

//Calculates the number of each library image in repeat range around x,y
//Only needs to look at first half of cells as the latter half are not yet used
std::map<size_t, int> PhotomosaicGenerator::calculateRepeats(
        const std::vector<std::vector<size_t>> &grid, const cv::Point &gridSize,
        const int x, const int y) const
{
    std::map<size_t, int> repeats;
    const int repeatStartX = std::clamp(x - m_repeatRange, 0, gridSize.x);
    const int repeatStartY = std::clamp(y - m_repeatRange, 0, gridSize.y);
    const int repeatEndX = std::clamp(x + m_repeatRange, 0, gridSize.x);

    //Looks at cells above the current cell
    for (int repeatY = repeatStartY; repeatY < std::clamp(y, 0, gridSize.y); ++repeatY)
    {
        for (int repeatX = repeatStartX; repeatX < repeatEndX; ++repeatX)
        {
            const auto it = repeats.find(grid.at(static_cast<size_t>(repeatX)).
                                   at(static_cast<size_t>(repeatY)));
            if (it != repeats.end())
                ++it->second;
            else
                repeats.emplace(grid.at(static_cast<size_t>(repeatX)).
                                at(static_cast<size_t>(repeatY)), 1);
        }
    }

    //Looks at cells directly to the left of current cell
    for (int repeatX = repeatStartX; repeatX < x; ++repeatX)
    {
        const auto it = repeats.find(grid.at(static_cast<size_t>(repeatX)).
                               at(static_cast<size_t>(y)));
        if (it != repeats.end())
            ++it->second;
        else
            repeats.emplace(grid.at(static_cast<size_t>(repeatX)).
                            at(static_cast<size_t>(y)), 1);
    }
    return repeats;
}

double PhotomosaicGenerator::degToRad(const double deg) const
{
    return (deg * M_PI) / 180;
}
