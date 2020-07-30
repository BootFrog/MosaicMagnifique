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

#include "reduction.cuh"

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <device_launch_parameters.h>

#include "cudaphotomosaicdata.h"

//Performs sum reduction in a single warp
template <size_t blockSize>
__device__
void warpReduceAdd(volatile double *sdata, const size_t tid)
{
    if (blockSize >= 64)
        sdata[tid] += sdata[tid + 32];
    if (blockSize >= 32)
        sdata[tid] += sdata[tid + 16];
    if (blockSize >= 16)
        sdata[tid] += sdata[tid + 8];
    if (blockSize >= 8)
        sdata[tid] += sdata[tid + 4];
    if (blockSize >= 4)
        sdata[tid] += sdata[tid + 2];
    if (blockSize >= 2)
        sdata[tid] += sdata[tid + 1];

}

//Performs sum reduction
template <size_t blockSize>
__global__
void reduceAdd(double *g_idata, double *g_odata, const size_t N, const size_t noLibIm)
{
    extern __shared__ double sdata[];

    for (size_t libI = 0; libI < noLibIm; ++libI)
    {
        size_t offset = libI * N;
        //Each thread loads atleast one element from global to shared memory
        size_t tid = threadIdx.x;
        size_t i = blockIdx.x * blockSize * 2 + threadIdx.x;
        size_t gridSize = blockSize * 2 * gridDim.x;
        sdata[tid] = 0;

        while (i < N)
        {
            sdata[tid] += (i + blockSize < N) ?
                        g_idata[i + offset] + g_idata[i + blockSize + offset] : g_idata[i + offset];
            i += gridSize;
        }
        __syncthreads();

        //Do reduction in shared memory
        if (blockSize >= 2048)
        {
            if (tid < 1024)
                sdata[tid] += sdata[tid + 1024];
            __syncthreads();
        }
        if (blockSize >= 1024)
        {
            if (tid < 512)
                sdata[tid] += sdata[tid + 512];
            __syncthreads();
        }
        if (blockSize >= 512)
        {
            if (tid < 256)
                sdata[tid] += sdata[tid + 256];
            __syncthreads();
        }
        if (blockSize >= 256)
        {
            if (tid < 128)
                sdata[tid] += sdata[tid + 128];
            __syncthreads();
        }
        if (blockSize >= 128)
        {
            if (tid < 64)
                sdata[tid] += sdata[tid + 64];
            __syncthreads();
        }

        if (tid < 32)
            warpReduceAdd<blockSize>(sdata, tid);

        //Write result for this block to global memory
        if (tid == 0)
            g_odata[blockIdx.x + libI * gridDim.x] = sdata[0];
    }
}

void reduceAddData(CUDAPhotomosaicData &photomosaicData, cudaStream_t stream[8],
                   const size_t noOfStreams)
{
    size_t curStream = 0;
    size_t reduceDataSize = photomosaicData.pixelCount;

    //Number of blocks needed assuming max block size
    size_t numBlocks = ((reduceDataSize + photomosaicData.getBlockSize() - 1)
                        / photomosaicData.getBlockSize() + 1) / 2;

    //Minimum number of threads per block
    size_t reduceBlockSize;

    //Stores number of threads to use per block (power of 2)
    size_t threads = photomosaicData.getBlockSize();

    do
    {
        //Calculate new number of blocks and threads
        numBlocks = ((reduceDataSize + photomosaicData.getBlockSize() - 1)
                     / photomosaicData.getBlockSize() + 1) / 2;
        reduceBlockSize = (reduceDataSize + numBlocks - 1) / numBlocks;
        while (threads > reduceBlockSize * 2)
            threads >>= 1;

        //Loop over all data in batch
        for (size_t i = 0; i < photomosaicData.getBatchSize()
             && photomosaicData.getBatchIndex() * photomosaicData.getBatchSize() + i
             < photomosaicData.noValidCells; ++i)
        {
            //Skip if cell invalid
            if (!photomosaicData.getCellState(i))
                continue;

            //Reduce
            switch (threads)
            {
            case 2048:
                reduceAdd<2048><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 1024:
                reduceAdd<1024><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 512:
                reduceAdd<512><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 256:
                reduceAdd<256><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 128:
                reduceAdd<128><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 64:
                reduceAdd<64><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 32:
                reduceAdd<32><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 16:
                reduceAdd<16><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 8:
                reduceAdd<8><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 4:
                reduceAdd<4><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 2:
                reduceAdd<2><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            case 1:
                reduceAdd<1><<<static_cast<unsigned int>(numBlocks),
                        static_cast<unsigned int>(threads),
                        static_cast<unsigned int>(threads * sizeof(double)), stream[curStream]
                        >>>(photomosaicData.getVariants(i), photomosaicData.getReductionMemory(i),
                            reduceDataSize, photomosaicData.noLibraryImages);
                break;
            }

            //Move to next stream
            ++curStream;
            if (curStream == noOfStreams)
                curStream = 0;
        }
        //Loop over all data in batch
        for (size_t i = 0; i < photomosaicData.getBatchSize()
             && photomosaicData.getBatchIndex() * photomosaicData.getBatchSize() + i
             < photomosaicData.noValidCells; ++i)
        {
            //Skip if cell invalid
            if (!photomosaicData.getCellState(i))
                continue;

            //Copy results back to data
            gpuErrchk(cudaMemcpy(photomosaicData.getVariants(i),
                                 photomosaicData.getReductionMemory(i),
                                 numBlocks * photomosaicData.noLibraryImages * sizeof(double),
                                 cudaMemcpyDeviceToDevice));
        }

        //New data length is equal to number of blocks
        reduceDataSize = numBlocks;
    }
    while (numBlocks > 1); //Keep reducing until only 1 block was used
}