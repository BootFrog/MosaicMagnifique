#include "gridviewer.h"

#include <QPainter>
#include <QDebug>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <QWheelEvent>

GridViewer::GridViewer(QWidget *parent)
    : QWidget(parent), MIN_ZOOM{0.5}, MAX_ZOOM{10}, zoom{1}
{
    layout = new QGridLayout(this);

    labelZoom = new QLabel("Zoom:", this);
    labelZoom->setStyleSheet("QWidget {"
                             "background-color: rgb(60, 60, 60);"
                             "color: rgb(255, 255, 255);"
                             "border-color: rgb(0, 0, 0);"
                             "}");
    layout->addWidget(labelZoom, 0, 0);

    spinZoom = new QDoubleSpinBox(this);
    spinZoom->setStyleSheet("QWidget {"
                           "background-color: rgb(60, 60, 60);"
                           "color: rgb(255, 255, 255);"
                           "}"
                           "QDoubleSpinBox {"
                           "border: 1px solid dimgray;"
                           "}");
    spinZoom->setRange(MIN_ZOOM * 100, MAX_ZOOM * 100);
    spinZoom->setValue(zoom * 100);
    spinZoom->setSuffix("%");
    spinZoom->setButtonSymbols(QDoubleSpinBox::PlusMinus);
    connect(spinZoom, SIGNAL(valueChanged(double)), this, SLOT(zoomChanged(double)));
    layout->addWidget(spinZoom, 0, 1);

    checkEdgeDetect = new QCheckBox("Edge Detect:", this);
    checkEdgeDetect->setLayoutDirection(Qt::LayoutDirection::RightToLeft);
    checkEdgeDetect->setStyleSheet("QWidget {"
                                   "background-color: rgb(60, 60, 60);"
                                   "color: rgb(255, 255, 255);"
                                   "border-color: rgb(0, 0, 0);"
                                   "}");
    checkEdgeDetect->setCheckState(Qt::Checked);
    connect(checkEdgeDetect, SIGNAL(stateChanged(int)), this, SLOT(edgeDetectChanged(int)));
    layout->addWidget(checkEdgeDetect, 0, 2);

    hSpacer = new QSpacerItem(10, 10, QSizePolicy::Expanding, QSizePolicy::Minimum);
    layout->addItem(hSpacer, 0, 3);

    vSpacer = new QSpacerItem(10, 10, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem(vSpacer, 1, 0);
}

//Changes state of edge detection in grid preview
void GridViewer::setEdgeDetect(bool t_state)
{
    checkEdgeDetect->setChecked(t_state);
}

//Generates grid preview
void GridViewer::updateGrid()
{
    //No cell mask, no grid
    if (cellShape.getCellMask(0, 0).empty())
    {
        grid = QImage();
        return;
    }

    //Calculate grid size
    int gridHeight = static_cast<int>(height() / MIN_ZOOM);
    int gridWidth = static_cast<int>(width() / MIN_ZOOM);
    //If background larger then change grid size
    if (!background.isNull())
    {
        if (background.height() > gridHeight)
            gridHeight = background.height();

        if (background.width() > gridWidth)
            gridWidth = background.width();
    }

    cv::Mat newGrid(gridHeight, gridWidth, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    cv::Mat newEdgeGrid(gridHeight, gridWidth, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    const cv::Point gridSize = cellShape.calculateGridSize(newGrid.cols, newGrid.rows, 0);

    //Create all cells in grid
    for (int x = 0; x < gridSize.x; ++x)
    {
        for (int y = 0; y < gridSize.y; ++y)
        {
            const cv::Rect roi = cellShape.getRectAt(x, y)
                    & cv::Rect(0, 0, newGrid.cols, newGrid.rows);

            cv::Mat gridPart(newGrid, roi), edgeGridPart(newEdgeGrid, roi);

            //Calculate if and how current cell is flipped
            bool flipHorizontal = false, flipVertical = false;
            if (cellShape.getColFlipHorizontal() && x % 2 == 1)
                flipHorizontal = !flipHorizontal;
            if (cellShape.getRowFlipHorizontal() && y % 2 == 1)
                flipHorizontal = !flipHorizontal;
            if (cellShape.getColFlipVertical() && x % 2 == 1)
                flipVertical = !flipVertical;
            if (cellShape.getRowFlipVertical() && y % 2 == 1)
                flipVertical = !flipVertical;

            //Copy cell to grid
            cv::Mat mask(flipHorizontal ? (flipVertical ? cellFlippedHV : cellFlippedH)
                                        : (flipVertical ? cellFlippedV : cell),
                         cv::Rect(0, 0, gridPart.cols, gridPart.rows));
            cv::Mat edgeMask(flipHorizontal ? (flipVertical ? edgeCellFlippedHV : edgeCellFlippedH)
                                            : (flipVertical ? edgeCellFlippedV : edgeCell),
                             cv::Rect(0, 0, edgeGridPart.cols, edgeGridPart.rows));
            cv::bitwise_or(gridPart, mask, gridPart);
            cv::bitwise_or(edgeGridPart, edgeMask, edgeGridPart);
        }
    }

    grid = QImage(newGrid.data, newGrid.cols, newGrid.rows, static_cast<int>(newGrid.step),
                  QImage::Format_RGBA8888).copy();
    edgeGrid = QImage(newEdgeGrid.data, newEdgeGrid.cols, newEdgeGrid.rows,
                      static_cast<int>(newEdgeGrid.step), QImage::Format_RGBA8888).copy();
    update();
}

//Sets the cell shape
void GridViewer::setCellShape(const CellShape &t_cellShape)
{
    cellShape = t_cellShape;

    if (!cellShape.getCellMask(0, 0).empty())
    {
        //Converts cell mask to RGBA
        cv::Mat result;
        cv::cvtColor(cellShape.getCellMask(0, 0), result, cv::COLOR_GRAY2RGBA);
        //Make black pixels transparent
        int channels = result.channels();
        int nRows = result.rows;
        int nCols = result.cols * channels;
        if (result.isContinuous())
        {
            nCols *= nRows;
            nRows = 1;
        }
        uchar *p;
        for (int i = 0; i < nRows; ++i)
        {
            p = result.ptr<uchar>(i);
            for (int j = 0; j < nCols; j += channels)
            {
                if (p[j] == 0)
                    p[j+3] = 0;
            }
        }
        cell = result;

        //Add single pixel black transparent border to mask so that Canny cannot leave open edges
        cv::Mat maskWithBorder;
        cv::copyMakeBorder(result, maskWithBorder, 1, 1, 1, 1, cv::BORDER_CONSTANT,
                           cv::Scalar(0));
        //Use Canny to detect edge of cell mask and convert to RGBA
        cv::Mat edgeResult;
        cv::Canny(maskWithBorder, edgeResult, 100.0, 155.0);
        cv::cvtColor(edgeResult, edgeResult, cv::COLOR_GRAY2RGBA);

        //Make black pixels transparent
        channels = edgeResult.channels();
        nRows = edgeResult.rows;
        nCols = edgeResult.cols * channels;
        if (edgeResult.isContinuous())
        {
            nCols *= nRows;
            nRows = 1;
        }
        for (int i = 0; i < nRows; ++i)
        {
            p = edgeResult.ptr<uchar>(i);
            for (int j = 0; j < nCols; j += channels)
            {
                if (p[j] == 0)
                    p[j+3] = 0;
            }
        }
        edgeCell = edgeResult;

        //Create flipped cell and edge cell
        cv::flip(cell, cellFlippedH, 1);
        cv::flip(edgeCell, edgeCellFlippedH, 1);
        cv::flip(cell, cellFlippedV, 0);
        cv::flip(edgeCell, edgeCellFlippedV, 0);
        cv::flip(cell, cellFlippedHV, -1);
        cv::flip(edgeCell, edgeCellFlippedHV, -1);
    }
    else
    {
        cell.release();
        cellFlippedH.release();
        cellFlippedV.release();
        cellFlippedHV.release();

        edgeCell.release();
        edgeCellFlippedH.release();
        edgeCellFlippedV.release();
        edgeCellFlippedHV.release();
    }

    updateGrid();
}

//Returns a reference to the cell shape
CellShape &GridViewer::getCellShape()
{
    return cellShape;
}

//Sets the background image in grid
void GridViewer::setBackground(const cv::Mat &t_background)
{
    if (t_background.empty())
        background = QImage();
    else
    {
        cv::Mat inverted(t_background.rows, t_background.cols, t_background.type());
        cv::cvtColor(t_background, inverted, cv::COLOR_BGR2RGB);

        background = QImage(inverted.data, inverted.cols, inverted.rows,
                            static_cast<int>(inverted.step), QImage::Format_RGB888).copy();

        //Grid too small for background, recreate
        if ((grid.height() < background.height()) ||
                (grid.width() < background.width()))
            updateGrid();
    }
    update();
}

//Called when the spinbox value is changed, updates grid zoom
void GridViewer::zoomChanged(double t_value)
{
    zoom = t_value / 100.0;
    update();
}

//Called when the checkbox state changes
void GridViewer::edgeDetectChanged(int /*t_state*/)
{
    update();
}

//Displays grid
void GridViewer::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);

    double ratio;
    QRectF sourceRect(0, 0, 0, 0);

    //Draw background
    if (!background.isNull())
    {
        //Calculate ratio between background and GridViewer size
        ratio = background.width() / static_cast<double>(width());
        if (height() * ratio < background.height())
            ratio = background.height() / static_cast<double>(height());

        sourceRect.setSize(QSizeF((ratio * width()) / zoom, (ratio * height()) / zoom));

        painter.drawImage(QRectF(QPointF(0,0), QSizeF(width(), height())), background, sourceRect);
    }
    else
        sourceRect.setSize(QSizeF(width() / zoom, height() / zoom));

    //Draw grid
    if (checkEdgeDetect->isChecked())
    {
        if (!edgeGrid.isNull())
        {
            QImage croppedGrid = edgeGrid.copy(0, 0, background.width(), background.height());
            painter.drawImage(QRectF(QPointF(0,0), QSizeF(width(), height())),
                              croppedGrid, sourceRect);
        }
    }
    else if (!grid.isNull())
    {
        QImage croppedGrid = grid.copy(0, 0, background.width(), background.height());
        painter.drawImage(QRectF(QPointF(0,0), QSizeF(width(), height())), croppedGrid, sourceRect);
    }
}

//Generate new grid with new size
void GridViewer::resizeEvent(QResizeEvent * /*event*/)
{
    updateGrid();
}

//Change zoom of grid preview based on mouse scrollwheel movement
//Ctrl is a modifier key that allows for faster zooming (x10)
void GridViewer::wheelEvent(QWheelEvent *event)
{
    zoom += event->delta() / ((event->modifiers().testFlag(Qt::ControlModifier)) ? 1200.0 : 12000.0);
    zoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);

    spinZoom->blockSignals(true);
    spinZoom->setValue(zoom * 100);
    spinZoom->blockSignals(false);

    update();
}
