#pragma once

#include <ios>

#ifndef BOOST_FILESYSTEM_NO_DEPERECATED
#define BOOST_FILESYSTEM_NO_DEPERECATED
#endif
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "z5/io/io_base.hxx"
#include "z5/types/types.hxx"
#include "z5/util/util.hxx"

namespace fs = boost::filesystem;

namespace z5 {
namespace io {

    // TODO 
    // endianess mess
    // the best way would be to handle this
    // in a streambuffer for boost::iostreams
    //

    template<typename T>
    class ChunkIoN5 : public ChunkIoBase<T> {

    public:

        ChunkIoN5(const types::ShapeType & shape, const types::ShapeType & chunkShape) :
            shape_(shape), chunkShape_(chunkShape){
        }

        inline bool read(const handle::Chunk & chunk, std::vector<T> & data) const {

            // if the chunk exists, we read it
            if(chunk.exists()) {

                // this might speed up the I/O by decoupling C++ buffers from C buffers
                std::ios_base::sync_with_stdio(false);
                // open input stream and read the header
                fs::ifstream file(chunk.path(), std::ios::binary);
                types::ShapeType chunkShape;
                size_t fileSize = readHeader(file, chunkShape);

                // resize the data vector
                size_t vectorSize = fileSize / sizeof(T) + (fileSize % sizeof(T) == 0 ? 0 : sizeof(T));
                data.resize(vectorSize);

                // read the file
                file.read((char*) &data[0], fileSize);
                file.close();

                // return true, because we have read an existing chunk
                return true;

            } else {
                return false;
            }

        }


        inline void write(const handle::Chunk & chunk, const T * data, const size_t chunkSize) const {
            // create the parent folder
            chunk.createTopDir();
            // this might speed up the I/O by decoupling C++ buffers from C buffers
            std::ios_base::sync_with_stdio(false);
            fs::ofstream file(chunk.path(), std::ios::binary);
            // write the header
            writeHeader(chunk, file);
            file.write((char*) data, chunkSize * sizeof(T));
            file.close();
        }


        inline void getChunkShape(const handle::Chunk & chunk, types::ShapeType & shape) const {
            if(chunk.exists()) {
                std::ios_base::sync_with_stdio(false);
                fs::ifstream file(chunk.path(), std::ios::binary);
                readHeader(file, shape);
                file.close();

            }
            else {
                chunk.boundedChunkShape(shape_, chunkShape_, shape);
            }
        }


        inline size_t getChunkSize(const handle::Chunk & chunk) const {
            types::ShapeType shape;
            getChunkShape(chunk, shape);
            return std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());
        }


        inline void findMinimumChunk(types::ShapeType & minOut, const fs::path & dsDir, const size_t nChunksTotal) {
            minOut.clear();
            fs::path chunkDir(dsDir);
            while(true) {
                // we need to pass something that is definetely bigger than any chunk id
                // as initial value here
                size_t chunkId = iterateChunks(chunkDir, nChunksTotal, std::min);
                minOut.push_back(chunkId);
                chunkDir /= std::to_string(chunkId);
                // we need to check if the next chunkDir is still a directory
                // or already a chunk file (and break)
                if(fs::is_regular_file(chunkDir)) {
                    break;
                }
            }
            // need to reverse due to n5 axis ordering
            std::reverse(minOut.begin(), minOut.end());
        }


        inline void findMaximumChunk(types::ShapeType & maxOut, const fs::path & dsDir) {
            
        }


    private:

        // go through all chunks in this directory and return the chunk that is optimal w.r.t compare (max or min)
        inline size_t iterateChunks(const fs::path & chunkDir, const size_t init, std::function<size_t (size_t, size_t)> compare) {
            fs::directory_iterator it(chunkDir);
            size_t ret = init;
            for(; it != fs::directory_iterator(); ++it) {
               ret = compare(ret, std::stoull(it->path().filename().string()));
            }
            return ret;
        }

        // TODO allow for reading the mode
        inline size_t readHeader(fs::ifstream & file, types::ShapeType & shape) const {

            // read the mode
            uint16_t mode;
            file.read((char *) &mode, 2);
            util::reverseEndiannessInplace(mode);

            // TODO support varlength mode
            if(mode != 0) {
                throw std::runtime_error("Zarr++ only supports reading N5 chunks in default mode");
            }

            // read the number of dimensions
            uint16_t nDims;
            file.read((char *) &nDims, 2);
            util::reverseEndiannessInplace(nDims);

            // read tempory shape with uint32 entries
            std::vector<uint32_t> shapeTmp(nDims);
            for(int d = 0; d < nDims; ++d) {
                file.read((char *) &shapeTmp[d], 4);
            }
            util::reverseEndiannessInplace<uint32_t>(shapeTmp.begin(), shapeTmp.end());

            // N5-Axis order: we need to reverse the chunk shape read from the header
            std::reverse(shapeTmp.begin(), shapeTmp.end());

            // copy the tempory shape to out shape
            shape.resize(nDims);
            std::copy(shapeTmp.begin(), shapeTmp.end(), shape.begin());

            // TODO need to read the actual size if we allow for varlength mode
            // calculate the file size
            size_t fileSize = std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<size_t>());
            return fileSize * sizeof(T);
        }

        inline void writeHeader(const handle::Chunk & chunk, fs::ofstream & file) const {

            // write the mode
            uint16_t mode = 0; // TODO support the varlength mode as well
            util::reverseEndiannessInplace(mode);
            file.write((char*) &mode, 2);

            // write the number of dimensions
            uint16_t nDimsOut = shape_.size();
            util::reverseEndiannessInplace(nDimsOut);
            file.write((char *) &nDimsOut, 2);

            // TODO need to invert the dimensions here
            // get the bounded chunk shape and write it to file
            std::vector<uint32_t> shapeOut(shape_.size());
            chunk.boundedChunkShape(shape_, chunkShape_, shapeOut);
            util::reverseEndiannessInplace<uint32_t>(shapeOut.begin(), shapeOut.end());

            // N5-Axis order: we need to reverse the chunk shape written to the header
            std::reverse(shapeOut.begin(), shapeOut.end());
            // write chunk shape to header
            for(int d = 0; d < shape_.size(); ++d) {
                file.write((char *) &shapeOut[d], 4);
            }

            // TODO need to write the actual size if we allow for varlength mode
        }

        // members
        const types::ShapeType & shape_;
        const types::ShapeType & chunkShape_;

    };


}
}
