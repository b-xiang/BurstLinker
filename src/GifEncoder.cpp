//
// Created by succlz123 on 17-9-5.
//

#include <cstdint>
#include <fstream>
#include "GifEncoder.h"
#include "GifBlockWriter.h"
#include "GifLogger.h"
#include "ColorQuantizer.h"
#include "KMeansQuantizer.h"
#include "MedianCutQuantizer.h"
#include "OctreeQuantizer.h"
#include "RandomQuantizer.h"
#include "BayerDitherer.h"
#include "LzwEncoder.h"
#include "FloydSteinbergDitherer.h"
#include "UniformQuantizer.h"
#include "NeuQuantQuantizer.h"
#include "M2Ditherer.h"
#include "NoDitherer.h"

using namespace std;

#if defined(__Android__)

#include <RenderScript.h>
#include "../android/lib/src/main/cpp/DisableDithererWithRs.h"
#include "../android/lib/src/main/cpp/BayerDithererWithRs.h"

using namespace android::RSC;

sp<RS> rs = nullptr;

#elif defined(__Other__)

#endif

int getColorTableSizeField(int actualTableSize) {
    int size = 0;
    while (1 << (size + 1) < actualTableSize) {
        ++size;
    }
    return size;
}

GifEncoder::~GifEncoder() {
    screenWidth = 0;
    screenHeight = 0;
    debugLog = false;
#if defined(__Android__)
    if (rs != nullptr) {
        rs.clear();
        rs = nullptr;
    }
#endif
    outfile.close();
    delete threadPool;
    delete[] rsCacheDir;
}

bool GifEncoder::init(const char *path, uint16_t width, uint16_t height, uint32_t loopCount,
                      uint32_t threadCount) {
    outfile.open(path, ios::out | ios::binary);
    if (!outfile.is_open()) {
        return false;
    }
    this->screenWidth = width;
    this->screenHeight = height;
    GifBlockWriter::writeHeaderBlock(outfile);
    GifBlockWriter::writeLogicalScreenDescriptorBlock(outfile, screenWidth, screenHeight, false, 1,
                                                      false, 0, 0, 0);
    GifBlockWriter::writeNetscapeLoopingExtensionBlock(outfile, loopCount);
    if (threadCount > 8) {
        threadCount = 8;
    }
    if (threadCount > 1) {
        threadPool = new ThreadPool(threadCount);
    }
    GifLogger::log(debugLog, "Image size is " + GifLogger::toString(width * height));
    return true;
}

vector<uint8_t> GifEncoder::addImage(uint32_t *originalColors, uint32_t delay,
                                     QuantizerType quantizerType, DitherType ditherType,
                                     uint16_t left, uint16_t top,
                                     vector<uint8_t> &content) {
    GifLogger::log(debugLog, "Get image pixel");

    ColorQuantizer *colorQuantizer = nullptr;
    string quantizerStr;
    switch (quantizerType) {
        case QuantizerType::Uniform:
        default:
            colorQuantizer = new UniformQuantizer();
            quantizerStr = "UniformQuantizer";
            break;
        case QuantizerType::MedianCut:
            colorQuantizer = new MedianCutQuantizer();
            quantizerStr = "MedianCutQuantizer";
            break;
        case QuantizerType::KMeans:
            colorQuantizer = new KMeansQuantizer();
            quantizerStr = "KMeansQuantizer";
            break;
        case QuantizerType::Random:
            colorQuantizer = new RandomQuantizer();
            quantizerStr = "RandomQuantizer";
            break;
        case QuantizerType::Octree:
            colorQuantizer = new OctreeQuantizer();
            quantizerStr = "OctreeQuantizer";
            break;
        case QuantizerType::NeuQuant:
            colorQuantizer = new NeuQuantQuantizer();
            quantizerStr = "NeuQuantQuantizer";
            break;
    }

    colorQuantizer->width = screenWidth;
    colorQuantizer->height = screenHeight;

    size_t colorSize = screenWidth * screenHeight;

    uint8_t *quantizerColors = nullptr;
    int32_t quantizerSize = 0;
    if (colorSize > 256) {
        quantizerSize = colorQuantizer->quantize(originalColors, colorSize, 256);
        quantizerColors = new uint8_t[(quantizerSize + 1) * 3];
        colorQuantizer->getColorPalette(quantizerColors);
    } else {
        int quantizerIndex = 0;
        quantizerColors = new uint8_t[(colorSize + 1) * 3];
        for (uint32_t i = 0; i < colorSize; ++i) {
            uint32_t color = originalColors[colorSize];
            quantizerColors[quantizerIndex++] = static_cast<uint8_t>((color) & 0xFF);
            quantizerColors[quantizerIndex++] = static_cast<uint8_t>((color >> 8) & 0xFF);
            quantizerColors[quantizerIndex++] = static_cast<uint8_t>((color >> 16) & 0xFF);
        }
    }
    GifLogger::log(debugLog, quantizerStr + " size is " + GifLogger::toString(quantizerSize));

    if (quantizerSize <= 0) {
        return content;
    }

    Ditherer *ditherer = nullptr;

    string dithererStr;

#if defined(__Android__)
    bool useRenderScript = false;
    if (rsCacheDir != nullptr) {
        if (rs == nullptr) {
            rs = new RS();
        }
        if (!rs.get()->getContext()) {
            useRenderScript = rs->init(rsCacheDir);
        } else {
            useRenderScript = true;
        }
    }
#endif

#if defined(__Android__)
    switch (ditherType) {
        case DitherType::NO:
        default:
            if (useRenderScript) {
                ditherer = new DisableDithererWithRs();
                dithererStr = "DisableDithererWithRs";
            } else {
                ditherer = new NoDitherer();
                dithererStr = "NoDitherer";
            }
            break;
        case DitherType::M2:
            useRenderScript = false;
            ditherer = new M2Ditherer();
            dithererStr = "M2Ditherer";
            break;
        case DitherType::Bayer:
            if (useRenderScript) {
                ditherer = new BayerDithererWithRs();
                dithererStr = "BayerDithererWithRs";
            } else {
                ditherer = new BayerDitherer();
                dithererStr = "BayerDitherer";
            }
            break;
        case DitherType::FloydSteinberg:
            useRenderScript = false;
            ditherer = new FloydSteinbergDitherer();
            dithererStr = "FloydSteinbergDitherer";
            break;
    }
#elif defined(__Other__)
    switch (ditherType) {
        default:
        case DitherType::NO:
            ditherer = new NoDitherer();
            dithererStr = "NoDitherer";
            break;
        case DitherType::M2:
            ditherer = new M2Ditherer();
            dithererStr = "M2Ditherer";
            break;
        case DitherType::Bayer:
            ditherer = new BayerDitherer();
            dithererStr = "BayerDitherer";
            break;
        case DitherType::FloydSteinberg:
            ditherer = new FloydSteinbergDitherer();
            dithererStr = "FloydSteinbergDitherer";
            break;
    }
#endif

    ditherer->quantizerType = quantizerType;
    ditherer->colorQuantizer = colorQuantizer;

#if defined(__Android__)
    if (useRenderScript) {
        static_cast<DithererWithRs *>(ditherer)->dither(originalColors, screenWidth, screenHeight,
                                                        quantizerColors,
                                                        quantizerSize, rs);
    } else {
        ditherer->dither(originalColors, screenWidth, screenHeight, quantizerColors,
                         quantizerSize);
    }
#elif defined(__Other__)
    ditherer->dither(originalColors, screenWidth, screenHeight, quantizerColors,
                     quantizerSize);
#endif

    auto *colorIndices = new uint32_t[colorSize];
    ditherer->getColorIndices(colorIndices, colorSize);
    delete colorQuantizer;
    delete ditherer;

    GifLogger::log(debugLog, dithererStr);

//    int32_t paddedColorCount = GifBlockWriter::paddedSize(quantizerSize);
    int32_t paddedColorCount = 256;

    GifBlockWriter::writeGraphicsControlExtensionBlock(content, 0, false, false, delay / 10, 0);
    GifBlockWriter::writeImageDescriptorBlock(content, left, top, screenWidth, screenHeight, true,
                                              false,
                                              false,
                                              getColorTableSizeField(paddedColorCount));
    GifBlockWriter::writeColorTable(content, quantizerColors, quantizerSize, paddedColorCount);
    delete[] quantizerColors;

    auto *lzwData = new char[colorSize]{0};
    LzwEncoder lzwEncoder(paddedColorCount);
    lzwEncoder.encode(colorIndices, screenWidth, screenHeight, colorSize, lzwData, content);
    GifLogger::log(debugLog, "LZW encode");
    delete[] colorIndices;
    delete[] lzwData;
    return content;
}

void GifEncoder::flush(vector<uint8_t> &content) {
    uint64_t size = content.size();
    for (int i = 0; i < size; ++i) {
        outfile.write((char *) (&content[i]), 1);
    }
}

void GifEncoder::finishEncoding() {
    GifBlockWriter::writeTerminator(outfile);
}
