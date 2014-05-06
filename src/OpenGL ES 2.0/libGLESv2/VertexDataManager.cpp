//
// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// VertexDataManager.h: Defines the VertexDataManager, a class that
// runs the Buffer translation process.

#include "VertexDataManager.h"

#include "Buffer.h"
#include "Program.h"
#include "main.h"
#include "IndexDataManager.h"
#include "common/debug.h"

namespace
{
    enum {INITIAL_STREAM_BUFFER_SIZE = 1024 * 1024};
}

namespace gl
{

VertexDataManager::VertexDataManager(Context *context, Device *device) : mContext(context), mDevice(device)
{
    for(int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
    {
        mDirtyCurrentValue[i] = true;
        mCurrentValueBuffer[i] = NULL;
    }

    mStreamingBuffer = new StreamingVertexBuffer(mDevice, INITIAL_STREAM_BUFFER_SIZE);

    if(!mStreamingBuffer)
    {
        ERR("Failed to allocate the streaming vertex buffer.");
    }
}

VertexDataManager::~VertexDataManager()
{
    delete mStreamingBuffer;

    for(int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
    {
        delete mCurrentValueBuffer[i];
    }
}

UINT VertexDataManager::writeAttributeData(ArrayVertexBuffer *vertexBuffer, GLint start, GLsizei count, const VertexAttribute &attribute)
{
    Buffer *buffer = attribute.mBoundBuffer.get();

    int inputStride = attribute.stride();
    int elementSize = attribute.typeSize();
    UINT streamOffset = 0;

    char *output = NULL;
    
    if(vertexBuffer)
    {
        output = (char*)vertexBuffer->map(attribute, spaceRequired(attribute, count), &streamOffset);
    }

    if(output == NULL)
    {
        ERR("Failed to map vertex buffer.");
        return -1;
    }

    const char *input = NULL;

    if(buffer)
    {
        int offset = attribute.mOffset;

        input = static_cast<const char*>(buffer->data()) + offset;
    }
    else
    {
        input = static_cast<const char*>(attribute.mPointer);
    }

    input += inputStride * start;

    if(inputStride == elementSize)
    {
        memcpy(output, input, count * inputStride);
    }
    else
    {
		for(int i = 0; i < count; i++)
		{
			memcpy(output, input, elementSize);
			output += elementSize;
			input += inputStride;
		}
    }

    vertexBuffer->unmap();

    return streamOffset;
}

GLenum VertexDataManager::prepareVertexData(GLint start, GLsizei count, TranslatedAttribute *translated)
{
    if(!mStreamingBuffer)
    {
        return GL_OUT_OF_MEMORY;
    }

    const VertexAttributeArray &attribs = mContext->getVertexAttributes();
    Program *program = mContext->getCurrentProgram();

    for(int attributeIndex = 0; attributeIndex < MAX_VERTEX_ATTRIBS; attributeIndex++)
    {
        translated[attributeIndex].active = (program->getSemanticIndex(attributeIndex) != -1);
    }

    // Determine the required storage size per used buffer, and invalidate static buffers that don't contain matching attributes
    for(int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
    {
        if(translated[i].active && attribs[i].mArrayEnabled)
        {
            if(!attribs[i].mBoundBuffer)
            {
                mStreamingBuffer->addRequiredSpace(spaceRequired(attribs[i], count));
            }
        }
    }

    mStreamingBuffer->reserveRequiredSpace();
    
    // Perform the vertex data translations
    for(int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
    {
        if(translated[i].active)
        {
            if(attribs[i].mArrayEnabled)
            {
                Buffer *buffer = attribs[i].mBoundBuffer.get();

                if(!buffer && attribs[i].mPointer == NULL)
                {
                    // This is an application error that would normally result in a crash, but we catch it and return an error
                    ERR("An enabled vertex array has no buffer and no pointer.");
                    return GL_INVALID_OPERATION;
                }

                sw::Resource *staticBuffer = buffer ? buffer->getResource() : NULL;

                if(staticBuffer)
                {
					translated[i].vertexBuffer = staticBuffer;
					translated[i].offset = start * attribs[i].stride() + attribs[i].mOffset;
					translated[i].stride = attribs[i].stride();
                }
                else
                {
                    UINT streamOffset = writeAttributeData(mStreamingBuffer, start, count, attribs[i]);

					if(streamOffset == -1)
					{
						return GL_OUT_OF_MEMORY;
					}

					translated[i].vertexBuffer = mStreamingBuffer->getResource();
					translated[i].offset = streamOffset;
					translated[i].stride = attribs[i].typeSize();
                }

				switch(attribs[i].mType)
				{
				case GL_BYTE:           translated[i].type = sw::STREAMTYPE_SBYTE;  break;
				case GL_UNSIGNED_BYTE:  translated[i].type = sw::STREAMTYPE_BYTE;   break;
				case GL_SHORT:          translated[i].type = sw::STREAMTYPE_SHORT;  break;
				case GL_UNSIGNED_SHORT: translated[i].type = sw::STREAMTYPE_USHORT; break;
				case GL_FIXED:          translated[i].type = sw::STREAMTYPE_FIXED;  break;
				case GL_FLOAT:          translated[i].type = sw::STREAMTYPE_FLOAT;  break;
				default: UNREACHABLE(); translated[i].type = sw::STREAMTYPE_FLOAT;  break;
				}

				translated[i].count = attribs[i].mSize;
				translated[i].normalized = attribs[i].mNormalized;
            }
            else
            {
                if(mDirtyCurrentValue[i])
                {
                    delete mCurrentValueBuffer[i];
                    mCurrentValueBuffer[i] = new ConstantVertexBuffer(mDevice, attribs[i].mCurrentValue[0], attribs[i].mCurrentValue[1], attribs[i].mCurrentValue[2], attribs[i].mCurrentValue[3]);
                    mDirtyCurrentValue[i] = false;
                }

                translated[i].vertexBuffer = mCurrentValueBuffer[i]->getResource();

                translated[i].type = sw::STREAMTYPE_FLOAT;
				translated[i].count = 4;
                translated[i].stride = 0;
                translated[i].offset = 0;
            }
        }
    }

    return GL_NO_ERROR;
}

std::size_t VertexDataManager::spaceRequired(const VertexAttribute &attrib, std::size_t count) const
{
	return attrib.typeSize() * count;
}

VertexBuffer::VertexBuffer(Device *device, std::size_t size) : mDevice(device), mVertexBuffer(NULL)
{
    if(size > 0)
    {
        mVertexBuffer = new sw::Resource(size + 1024);
        
        if(!mVertexBuffer)
        {
            ERR("Out of memory allocating a vertex buffer of size %lu.", size);
        }
    }
}

VertexBuffer::~VertexBuffer()
{
    if(mVertexBuffer)
    {
        mVertexBuffer->destruct();
    }
}

void VertexBuffer::unmap()
{
    if(mVertexBuffer)
    {
		mVertexBuffer->unlock();
    }
}

sw::Resource *VertexBuffer::getResource() const
{
    return mVertexBuffer;
}

ConstantVertexBuffer::ConstantVertexBuffer(Device *device, float x, float y, float z, float w) : VertexBuffer(device, 4 * sizeof(float))
{
    void *buffer = NULL;

    if(mVertexBuffer)
    {
		buffer = mVertexBuffer->lock(sw::PUBLIC);
     
        if(!buffer)
        {
            ERR("Lock failed");
        }
    }

    if(buffer)
    {
        float *vector = (float*)buffer;

        vector[0] = x;
        vector[1] = y;
        vector[2] = z;
        vector[3] = w;

        mVertexBuffer->unlock();
    }
}

ConstantVertexBuffer::~ConstantVertexBuffer()
{
}

ArrayVertexBuffer::ArrayVertexBuffer(Device *device, std::size_t size) : VertexBuffer(device, size)
{
    mBufferSize = size;
    mWritePosition = 0;
    mRequiredSpace = 0;
}

ArrayVertexBuffer::~ArrayVertexBuffer()
{
}

void ArrayVertexBuffer::addRequiredSpace(UINT requiredSpace)
{
    mRequiredSpace += requiredSpace;
}

StreamingVertexBuffer::StreamingVertexBuffer(Device *device, std::size_t initialSize) : ArrayVertexBuffer(device, initialSize)
{
}

StreamingVertexBuffer::~StreamingVertexBuffer()
{
}

void *StreamingVertexBuffer::map(const VertexAttribute &attribute, std::size_t requiredSpace, std::size_t *offset)
{
    void *mapPtr = NULL;

    if(mVertexBuffer)
    {
		mapPtr = (char*)mVertexBuffer->lock(sw::PUBLIC) + mWritePosition;
        
        if(!mapPtr)
        {
            ERR("Lock failed");
            return NULL;
        }

        *offset = mWritePosition;
        mWritePosition += requiredSpace;
    }

    return mapPtr;
}

void StreamingVertexBuffer::reserveRequiredSpace()
{
    if(mRequiredSpace > mBufferSize)
    {
        if(mVertexBuffer)
        {
            mVertexBuffer->destruct();
            mVertexBuffer = 0;
        }

        mBufferSize = std::max(mRequiredSpace, 3 * mBufferSize / 2);   // 1.5 x mBufferSize is arbitrary and should be checked to see we don't have too many reallocations.

		mVertexBuffer = new sw::Resource(mBufferSize);
    
        if(!mVertexBuffer)
        {
            ERR("Out of memory allocating a vertex buffer of size %lu.", mBufferSize);
        }

        mWritePosition = 0;
    }
    else if(mWritePosition + mRequiredSpace > mBufferSize)   // Recycle
    {
        if(mVertexBuffer)
        {
            mVertexBuffer->destruct();
			mVertexBuffer = new sw::Resource(mBufferSize);
        }

        mWritePosition = 0;
    }

    mRequiredSpace = 0;
}

}