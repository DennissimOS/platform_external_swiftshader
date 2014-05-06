// SwiftShader Software Renderer
//
// Copyright(c) 2005-2011 TransGaming Inc.
//
// All rights reserved. No part of this software may be copied, distributed, transmitted,
// transcribed, stored in a retrieval system, translated into any human or computer
// language by any means, or disclosed to third parties without the explicit written
// agreement of TransGaming Inc. Without such an agreement, no rights or licenses, express
// or implied, including but not limited to any patent rights, are granted to you.
//

#include "Context.hpp"

#include "PixelShader.hpp"
#include "VertexShader.hpp"
#include "Primitive.hpp"
#include "Surface.hpp"
#include "Memory.hpp"
#include "CPUID.hpp"
#include "Debug.hpp"

#include <string.h>
#include <malloc.h>

namespace sw
{
	extern bool perspectiveCorrection;

	bool forceWindowed = false;
	bool quadLayoutEnabled = false;
	bool veryEarlyDepthTest = true;
	bool complementaryDepthBuffer = false;
	bool postBlendSRGB = false;
	bool exactColorRounding = false;
	Context::TransparencyAntialiasing transparencyAntialiasing = Context::TRANSPARENCY_NONE;
	bool forceClearRegisters = false;

	Context::Context()
	{
		init();
	}

	Context::~Context()
	{
	}

	void *Context::operator new(size_t bytes)
	{
		return allocate((unsigned int)bytes);
	}

	void Context::operator delete(void *pointer, size_t bytes)
	{
		deallocate(pointer);
	}

	bool Context::isDrawPoint(bool fillModeAware) const
	{
		switch(drawType)
		{
		case DRAW_POINTLIST:
		case DRAW_INDEXEDPOINTLIST8:
		case DRAW_INDEXEDPOINTLIST16:
		case DRAW_INDEXEDPOINTLIST32:
			return true;
		case DRAW_LINELIST:
		case DRAW_LINESTRIP:
		case DRAW_LINELOOP:
		case DRAW_INDEXEDLINELIST8:
		case DRAW_INDEXEDLINESTRIP8:
		case DRAW_INDEXEDLINELOOP8:
		case DRAW_INDEXEDLINELIST16:
		case DRAW_INDEXEDLINESTRIP16:
		case DRAW_INDEXEDLINELOOP16:
		case DRAW_INDEXEDLINELIST32:
		case DRAW_INDEXEDLINESTRIP32:
		case DRAW_INDEXEDLINELOOP32:
			return false;
		case DRAW_TRIANGLELIST:
		case DRAW_TRIANGLESTRIP:
		case DRAW_TRIANGLEFAN:
		case DRAW_INDEXEDTRIANGLELIST8:
		case DRAW_INDEXEDTRIANGLESTRIP8:
		case DRAW_INDEXEDTRIANGLEFAN8:
		case DRAW_INDEXEDTRIANGLELIST16:
		case DRAW_INDEXEDTRIANGLESTRIP16:
		case DRAW_INDEXEDTRIANGLEFAN16:
		case DRAW_INDEXEDTRIANGLELIST32:
		case DRAW_INDEXEDTRIANGLESTRIP32:
		case DRAW_INDEXEDTRIANGLEFAN32:
			return fillModeAware ? fillMode == FILL_VERTEX : false;
		default:
			ASSERT(false);
		}

		return false;
	}

	bool Context::isDrawLine(bool fillModeAware) const
	{
		switch(drawType)
		{
		case DRAW_POINTLIST:
		case DRAW_INDEXEDPOINTLIST8:
		case DRAW_INDEXEDPOINTLIST16:
		case DRAW_INDEXEDPOINTLIST32:
			return false;
		case DRAW_LINELIST:
		case DRAW_LINESTRIP:
		case DRAW_LINELOOP:
		case DRAW_INDEXEDLINELIST8:
		case DRAW_INDEXEDLINESTRIP8:
		case DRAW_INDEXEDLINELOOP8:
		case DRAW_INDEXEDLINELIST16:
		case DRAW_INDEXEDLINESTRIP16:
		case DRAW_INDEXEDLINELOOP16:
		case DRAW_INDEXEDLINELIST32:
		case DRAW_INDEXEDLINESTRIP32:
		case DRAW_INDEXEDLINELOOP32:
			return true;
		case DRAW_TRIANGLELIST:
		case DRAW_TRIANGLESTRIP:
		case DRAW_TRIANGLEFAN:
		case DRAW_INDEXEDTRIANGLELIST8:
		case DRAW_INDEXEDTRIANGLESTRIP8:
		case DRAW_INDEXEDTRIANGLEFAN8:
		case DRAW_INDEXEDTRIANGLELIST16:
		case DRAW_INDEXEDTRIANGLESTRIP16:
		case DRAW_INDEXEDTRIANGLEFAN16:
		case DRAW_INDEXEDTRIANGLELIST32:
		case DRAW_INDEXEDTRIANGLESTRIP32:
		case DRAW_INDEXEDTRIANGLEFAN32:
			return fillModeAware ? fillMode == FILL_WIREFRAME : false;
		default:
			ASSERT(false);
		}

		return false;
	}

	bool Context::isDrawTriangle(bool fillModeAware) const
	{
		switch(drawType)
		{
		case DRAW_POINTLIST:
		case DRAW_INDEXEDPOINTLIST8:
		case DRAW_INDEXEDPOINTLIST16:
		case DRAW_INDEXEDPOINTLIST32:
			return false;
		case DRAW_LINELIST:
		case DRAW_LINESTRIP:
		case DRAW_LINELOOP:
		case DRAW_INDEXEDLINELIST8:
		case DRAW_INDEXEDLINESTRIP8:
		case DRAW_INDEXEDLINELOOP8:
		case DRAW_INDEXEDLINELIST16:
		case DRAW_INDEXEDLINESTRIP16:
		case DRAW_INDEXEDLINELOOP16:
		case DRAW_INDEXEDLINELIST32:
		case DRAW_INDEXEDLINESTRIP32:
		case DRAW_INDEXEDLINELOOP32:
			return false;
		case DRAW_TRIANGLELIST:
		case DRAW_TRIANGLESTRIP:
		case DRAW_TRIANGLEFAN:
		case DRAW_INDEXEDTRIANGLELIST8:
		case DRAW_INDEXEDTRIANGLESTRIP8:
		case DRAW_INDEXEDTRIANGLEFAN8:
		case DRAW_INDEXEDTRIANGLELIST16:
		case DRAW_INDEXEDTRIANGLESTRIP16:
		case DRAW_INDEXEDTRIANGLEFAN16:
		case DRAW_INDEXEDTRIANGLELIST32:
		case DRAW_INDEXEDTRIANGLESTRIP32:
		case DRAW_INDEXEDTRIANGLEFAN32:
			return fillModeAware ? fillMode == FILL_SOLID : true;
		default:
			ASSERT(false);
		}

		return true;
	}

	void Context::init()
	{
		for(int i = 0; i < 8; i++)
		{
			textureStage[i].init(i, &sampler[i], (i >= 1) ? &textureStage[i - 1] : 0);
		}

		// Set vertex streams to null stream
		for(int i = 0; i < 16; i++)
		{
			input[i].defaults();
		}

		postTransform = false;

		fogStart = 0.0f;
		fogEnd = 1.0f;

		for(int i = 0; i < 16; i++) textureWrap[i] = 0;
		for(int i = 0; i < 8; i++) texGen[i] = TEXGEN_PASSTHRU;
		for(int i = 0; i < 8; i++) textureTransformCount[i] = 0;
		for(int i = 0; i < 8; i++) textureTransformProject[i] = false;
		textureWrapActive = false;
		localViewer = true;
		normalizeNormals = false;
		
		renderTarget[0] = 0;
		renderTarget[1] = 0;
		renderTarget[2] = 0;
		renderTarget[3] = 0;
		depthStencil = 0;

		stencilEnable = false;
		stencilCompareMode = STENCIL_ALWAYS;
		stencilReference = 0;
		stencilMask = 0xFFFFFFFF;
		stencilFailOperation = OPERATION_KEEP;
		stencilPassOperation = OPERATION_KEEP;
		stencilZFailOperation = OPERATION_KEEP;
		stencilWriteMask = 0xFFFFFFFF;
		
		twoSidedStencil = false;
		stencilCompareModeCCW = STENCIL_ALWAYS;
		stencilReferenceCCW = 0;
		stencilMaskCCW = 0xFFFFFFFF;
		stencilFailOperationCCW = OPERATION_KEEP;
		stencilPassOperationCCW = OPERATION_KEEP;
		stencilZFailOperationCCW = OPERATION_KEEP;
		stencilWriteMaskCCW = 0xFFFFFFFF;

		setGlobalMipmapBias(0);

		lightingEnable = true;
		specularEnable = false;
		for(int i = 0; i < 8; i++) lightEnable[i] = false;
		for(int i = 0; i < 8; i++) worldLightPosition[i] = 0;

		alphaCompareMode = ALPHA_ALWAYS;
		alphaTestEnable = false;
		fillMode = FILL_SOLID;
		shadingMode = SHADING_GOURAUD;

		depthCompareMode = DEPTH_LESS;
		depthBufferEnable = true;
		depthWriteEnable = true;

		alphaBlendEnable = false;
		sourceBlendFactorState = BLEND_ONE;
		destBlendFactorState = BLEND_ZERO;
		blendOperationState = BLENDOP_ADD;

		separateAlphaBlendEnable = false;
		sourceBlendFactorStateAlpha = BLEND_ONE;
		destBlendFactorStateAlpha = BLEND_ZERO;
		blendOperationStateAlpha = BLENDOP_ADD;

		cullMode = CULL_CLOCKWISE;
		alphaReference = 0;
		colorWriteMask[0] = 0x0000000F;
		colorWriteMask[1] = 0x0000000F;
		colorWriteMask[2] = 0x0000000F;
		colorWriteMask[3] = 0x0000000F;

		ambientMaterialSource = MATERIAL;
		diffuseMaterialSource = COLOR1;
		specularMaterialSource = COLOR2;
		emissiveMaterialSource = MATERIAL;
		colorVertexEnable = true;

		fogEnable = false;
		pixelFogMode = FOG_NONE;
		vertexFogMode = FOG_NONE;
		wBasedFog = false;
		rangeFogEnable = false;

		indexedVertexBlendEnable = false;
		vertexBlendMatrixCount = 0;

		pixelShader = 0;
		vertexShader = 0;

		occlusionEnabled = false;

		pointSpriteEnable = false;
		pointScaleEnable = false;

		writeSRGB = false;
		sampleMask = 0xFFFFFFFF;
	}

	const float &Context::exp2Bias()
	{
		return bias;
	}

	const Point &Context::getLightPosition(int light)
	{
		return worldLightPosition[light];
	}

	void Context::setGlobalMipmapBias(float bias)
	{
		this->bias = exp2(bias + 0.5f);
	}

	void Context::setLightingEnable(bool lightingEnable)
	{
		this->lightingEnable = lightingEnable;
	}

	void Context::setSpecularEnable(bool specularEnable)
	{
		Context::specularEnable = specularEnable;
	}

	void Context::setLightEnable(int light, bool lightEnable)
	{
		Context::lightEnable[light] = lightEnable;
	}

	void Context::setLightPosition(int light, Point worldLightPosition)
	{
		Context::worldLightPosition[light] = worldLightPosition;
	}

	void Context::setAmbientMaterialSource(MaterialSource ambientMaterialSource)
	{
		Context::ambientMaterialSource = ambientMaterialSource;
	}

	void Context::setDiffuseMaterialSource(MaterialSource diffuseMaterialSource)
	{
		Context::diffuseMaterialSource = diffuseMaterialSource;
	}

	void Context::setSpecularMaterialSource(MaterialSource specularMaterialSource)
	{
		Context::specularMaterialSource = specularMaterialSource;
	}

	void Context::setEmissiveMaterialSource(MaterialSource emissiveMaterialSource)
	{
		Context::emissiveMaterialSource = emissiveMaterialSource;
	}
	
	void Context::setPointSpriteEnable(bool pointSpriteEnable)
	{
		Context::pointSpriteEnable = pointSpriteEnable;
	}

	void Context::setPointScaleEnable(bool pointScaleEnable)
	{
		Context::pointScaleEnable = pointScaleEnable;
	}

	bool Context::setDepthBufferEnable(bool depthBufferEnable)
	{
		bool modified = (Context::depthBufferEnable != depthBufferEnable);
		Context::depthBufferEnable = depthBufferEnable;
		return modified;
	}

	bool Context::setAlphaBlendEnable(bool alphaBlendEnable)
	{
		bool modified = (Context::alphaBlendEnable != alphaBlendEnable);
		Context::alphaBlendEnable = alphaBlendEnable;
		return modified;
	}

	bool Context::setSourceBlendFactor(BlendFactor sourceBlendFactor)
	{
		bool modified = (Context::sourceBlendFactorState != sourceBlendFactor);
		Context::sourceBlendFactorState = sourceBlendFactor;
		return modified;
	}

	bool Context::setDestBlendFactor(BlendFactor destBlendFactor)
	{
		bool modified = (Context::destBlendFactorState != destBlendFactor);
		Context::destBlendFactorState = destBlendFactor;
		return modified;
	}

	bool Context::setBlendOperation(BlendOperation blendOperation)
	{
		bool modified = (Context::blendOperationState != blendOperation);
		Context::blendOperationState = blendOperation;
		return modified;
	}

	bool Context::setSeparateAlphaBlendEnable(bool separateAlphaBlendEnable)
	{
		bool modified = (Context::separateAlphaBlendEnable != separateAlphaBlendEnable);
		Context::separateAlphaBlendEnable = separateAlphaBlendEnable;
		return modified;
	}

	bool Context::setSourceBlendFactorAlpha(BlendFactor sourceBlendFactorAlpha)
	{
		bool modified = (Context::sourceBlendFactorStateAlpha != sourceBlendFactorAlpha);
		Context::sourceBlendFactorStateAlpha = sourceBlendFactorAlpha;
		return modified;
	}

	bool Context::setDestBlendFactorAlpha(BlendFactor destBlendFactorAlpha)
	{
		bool modified = (Context::destBlendFactorStateAlpha != destBlendFactorAlpha);
		Context::destBlendFactorStateAlpha = destBlendFactorAlpha;
		return modified;
	}

	bool Context::setBlendOperationAlpha(BlendOperation blendOperationAlpha)
	{
		bool modified = (Context::blendOperationStateAlpha != blendOperationAlpha);
		Context::blendOperationStateAlpha = blendOperationAlpha;
		return modified;
	}

	bool Context::setColorWriteMask(int index, int colorWriteMask)
	{
		bool modified = (Context::colorWriteMask[index] != colorWriteMask);
		Context::colorWriteMask[index] = colorWriteMask;
		return modified;
	}

	bool Context::setWriteSRGB(bool sRGB)
	{
		bool modified = (Context::writeSRGB != sRGB);
		Context::writeSRGB = sRGB;
		return modified;
	}

	void Context::setColorVertexEnable(bool colorVertexEnable)
	{
		Context::colorVertexEnable = colorVertexEnable;
	}

	bool Context::fogActive()
	{
		if(!colorUsed()) return false;

		if(pixelShaderVersion() >= 0x0300) return false;

		return fogEnable;
	}

	bool Context::pointSizeActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return isDrawPoint(true) && (input[PSize] || (!preTransformed && pointScaleActive()));
	}

	Context::FogMode Context::pixelFogActive()
	{
		if(fogActive())
		{
			return pixelFogMode;
		}

		return FOG_NONE;
	}

	bool Context::depthWriteActive()
	{
		if(!depthBufferActive()) return false;

		return depthWriteEnable;
	}

	bool Context::alphaTestActive()
	{
		if(!alphaTestEnable) return false;
		if(alphaCompareMode == ALPHA_ALWAYS) return false;
		if(alphaReference == 0 && alphaCompareMode == ALPHA_GREATEREQUAL) return false;
		
		return true;
	}

	bool Context::depthBufferActive()
	{
		return depthStencil && depthBufferEnable;
	}

	bool Context::stencilActive()
	{
		return depthStencil && stencilEnable;
	}

	bool Context::vertexLightingActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return lightingEnable && !preTransformed;
	}

	bool Context::vertexTextureActive(int coordinate, int component)   // FIXME: Rename to texCoordActive
	{
		bool hasTexture = pointSpriteActive();

		if(vertexShader)
		{
			if(!preTransformed)
			{
				if(vertexShader->output[T0 + coordinate][component].usage == ShaderOperation::USAGE_TEXCOORD)
				{
					hasTexture = true;
				}
			}
			else
			{
				hasTexture = true;   // FIXME: Check vertex buffer streams
			}
		}
		else
		{
			switch(texGen[coordinate])
			{
			case TEXGEN_PASSTHRU:
				if(input[TexCoord0 + textureStage[coordinate].texCoordIndex].type == STREAMTYPE_FLOAT)
				{
					hasTexture = hasTexture || (component < input[TexCoord0 + textureStage[coordinate].texCoordIndex].count);
				}
				else ASSERT(!input[TexCoord0 + textureStage[coordinate].texCoordIndex]);
				break;
			case TEXGEN_NORMAL:
				hasTexture = hasTexture || (component <= 2);
				break;
			case TEXGEN_POSITION:
				hasTexture = hasTexture || (component <= 2);
				break;
			case TEXGEN_REFLECTION:
				hasTexture = hasTexture || (component <= 2);
				break;
			case TEXGEN_SPHEREMAP:
				hasTexture = hasTexture || (component <= 1);
				break;
			default:
				ASSERT(false);
			}
		}

		bool project = isProjectionComponent(coordinate, component);
		bool usesTexture = false;

		if(pixelShader)
		{
			usesTexture = pixelShader->usesTexture(coordinate, component) || project;
		}
		else
		{
			usesTexture = textureStage[coordinate].usesTexture() || project;
		}

		return hasTexture && usesTexture;
	}

	bool Context::vertexTextureActive(int coordinate)   // FIXME: Rename to texCoordActive
	{
		return vertexTextureActive(coordinate, 0) ||
		       vertexTextureActive(coordinate, 1) ||
			   vertexTextureActive(coordinate, 2) ||
			   vertexTextureActive(coordinate, 3);
	}

	bool Context::isProjectionComponent(unsigned int coordinate, int component)
	{
		if(pixelShaderVersion() <= 0x0103 && coordinate < 8 && textureTransformProject[coordinate])
		{
			if(textureTransformCount[coordinate] == 2)
			{
				if(component == 1) return true;
			}
			else if(textureTransformCount[coordinate] == 3)
			{
				if(component == 2) return true;
			}
			else if(textureTransformCount[coordinate] == 4 || textureTransformCount[coordinate] == 0)
			{
				if(component == 3) return true;
			}
		}

		return false;
	}

	bool Context::vertexSpecularActive()
	{
		return vertexLightingActive() && specularEnable && vertexNormalActive();
	}

	bool Context::vertexNormalActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return input[Normal];
	}

	bool Context::vertexLightActive(int i)
	{
		if(vertexShader)
		{
			return false;
		}

		return lightingEnable && lightEnable[i];
	}

	Context::MaterialSource Context::vertexDiffuseMaterialSourceActive()
	{
		if(vertexShader)
		{
			return MATERIAL;
		}

		if(diffuseMaterialSource == MATERIAL || !colorVertexEnable ||
		   (diffuseMaterialSource == COLOR1 && !input[Color0]) ||
		   (diffuseMaterialSource == COLOR2 && !input[Color1]))
		{
			return MATERIAL;
		}
		
		return diffuseMaterialSource;
	}

	Context::MaterialSource Context::vertexSpecularMaterialSourceActive()
	{
		if(vertexShader)
		{
			return MATERIAL;
		}

		if(specularMaterialSource == MATERIAL || !colorVertexEnable ||
		   (specularMaterialSource == COLOR1 && !input[Color0]) ||
		   (specularMaterialSource == COLOR2 && !input[Color1]))
		{
			return MATERIAL;
		}
		
		return specularMaterialSource;
	}

	Context::MaterialSource Context::vertexAmbientMaterialSourceActive()
	{
		if(vertexShader)
		{
			return MATERIAL;
		}

		if(ambientMaterialSource == MATERIAL || !colorVertexEnable ||
		   (ambientMaterialSource == COLOR1 && !input[Color0]) ||
		   (ambientMaterialSource == COLOR2 && !input[Color1]))
		{
			return MATERIAL;
		}
		
		return ambientMaterialSource;
	}

	Context::MaterialSource Context::vertexEmissiveMaterialSourceActive()
	{
		if(vertexShader)
		{
			return MATERIAL;
		}

		if(emissiveMaterialSource == MATERIAL || !colorVertexEnable ||
		   (emissiveMaterialSource == COLOR1 && !input[Color0]) ||
		   (emissiveMaterialSource == COLOR2 && !input[Color1]))
		{
			return MATERIAL;
		}
		
		return emissiveMaterialSource;
	}

	bool Context::pointSpriteActive()
	{
		return isDrawPoint(true) && pointSpriteEnable;
	}

	bool Context::pointScaleActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return isDrawPoint(true) && pointScaleEnable;
	}

	bool Context::alphaBlendActive()
	{
		if(!alphaBlendEnable)
		{
			return false;
		}

		if(!colorUsed())
		{
			return false;
		}

		bool colorBlend = !(blendOperation() == BLENDOP_SOURCE && sourceBlendFactor() == BLEND_ONE);
		bool alphaBlend = separateAlphaBlendEnable ? !(blendOperationAlpha() == BLENDOP_SOURCE && sourceBlendFactorAlpha() == BLEND_ONE) : colorBlend;

		return colorBlend || alphaBlend;
	}

	Context::BlendFactor Context::sourceBlendFactor()
	{
		if(!alphaBlendEnable) return BLEND_ONE;

		switch(blendOperationState)
		{
		case BLENDOP_ADD:
		case BLENDOP_SUB:
		case BLENDOP_INVSUB:
			return sourceBlendFactorState;
		case BLENDOP_MIN:
			return BLEND_ONE;
		case BLENDOP_MAX:
			return BLEND_ONE; 
		default:
			ASSERT(false);
		}

		return sourceBlendFactorState;
	}

	Context::BlendFactor Context::destBlendFactor()
	{
		if(!alphaBlendEnable) return BLEND_ZERO;

		switch(blendOperationState)
		{
		case BLENDOP_ADD:
		case BLENDOP_SUB:
		case BLENDOP_INVSUB:
			return destBlendFactorState;
		case BLENDOP_MIN:
			return BLEND_ONE;
		case BLENDOP_MAX:
			return BLEND_ONE; 
		default:
			ASSERT(false);
		}

		return destBlendFactorState;
	}

	Context::BlendOperation Context::blendOperation()
	{
		if(!alphaBlendEnable) return BLENDOP_SOURCE;

		switch(blendOperationState)
		{
		case BLENDOP_ADD:
			if(sourceBlendFactor() == BLEND_ZERO)
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_NULL;
				}
				else
				{
					return BLENDOP_DEST;
				}
			}
			else if(sourceBlendFactor() == BLEND_ONE)
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_SOURCE;
				}
				else
				{
					return BLENDOP_ADD;
				}
			}
			else
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_SOURCE;
				}
				else
				{
					return BLENDOP_ADD;
				}
			}
		case BLENDOP_SUB:
			if(sourceBlendFactor() == BLEND_ZERO)
			{
				return BLENDOP_NULL;   // Negative, clamped to zero
			}
			else if(sourceBlendFactor() == BLEND_ONE)
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_SOURCE;
				}
				else
				{
					return BLENDOP_SUB;
				}
			}
			else
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_SOURCE;
				}
				else
				{
					return BLENDOP_SUB;
				}
			}
		case BLENDOP_INVSUB:
			if(sourceBlendFactor() == BLEND_ZERO)
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_NULL;
				}
				else
				{
					return BLENDOP_DEST;
				}
			}
			else if(sourceBlendFactor() == BLEND_ONE)
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_NULL;   // Negative, clamped to zero
				}
				else
				{
					return BLENDOP_INVSUB;
				}
			}
			else
			{
				if(destBlendFactor() == BLEND_ZERO)
				{
					return BLENDOP_NULL;   // Negative, clamped to zero
				}
				else
				{
					return BLENDOP_INVSUB;
				}
			}
		case BLENDOP_MIN:
			return BLENDOP_MIN;
		case BLENDOP_MAX:
			return BLENDOP_MAX;
		default:
			ASSERT(false);
		}

		return blendOperationState;
	}

	Context::BlendFactor Context::sourceBlendFactorAlpha()
	{
		if(!separateAlphaBlendEnable)
		{
			return sourceBlendFactor();
		}
		else
		{
			switch(blendOperationStateAlpha)
			{
			case BLENDOP_ADD:
			case BLENDOP_SUB:
			case BLENDOP_INVSUB:
				return sourceBlendFactorStateAlpha;
			case BLENDOP_MIN:
				return BLEND_ONE;
			case BLENDOP_MAX:
				return BLEND_ONE; 
			default:
				ASSERT(false);
			}

			return sourceBlendFactorStateAlpha;
		}
	}

	Context::BlendFactor Context::destBlendFactorAlpha()
	{
		if(!separateAlphaBlendEnable)
		{
			return destBlendFactor();
		}
		else
		{
			switch(blendOperationStateAlpha)
			{
			case BLENDOP_ADD:
			case BLENDOP_SUB:
			case BLENDOP_INVSUB:
				return destBlendFactorStateAlpha;
			case BLENDOP_MIN:
				return BLEND_ONE;
			case BLENDOP_MAX:
				return BLEND_ONE; 
			default:
				ASSERT(false);
			}

			return destBlendFactorStateAlpha;
		}
	}

	Context::BlendOperation Context::blendOperationAlpha()
	{
		if(!separateAlphaBlendEnable)
		{
			return blendOperation();
		}
		else
		{
			switch(blendOperationStateAlpha)
			{
			case BLENDOP_ADD:
				if(sourceBlendFactorAlpha() == BLEND_ZERO)
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_NULL;
					}
					else
					{
						return BLENDOP_DEST;
					}
				}
				else if(sourceBlendFactorAlpha() == BLEND_ONE)
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_SOURCE;
					}
					else
					{
						return BLENDOP_ADD;
					}
				}
				else
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_SOURCE;
					}
					else
					{
						return BLENDOP_ADD;
					}
				}
			case BLENDOP_SUB:
				if(sourceBlendFactorAlpha() == BLEND_ZERO)
				{
					return BLENDOP_NULL;   // Negative, clamped to zero
				}
				else if(sourceBlendFactorAlpha() == BLEND_ONE)
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_SOURCE;
					}
					else
					{
						return BLENDOP_SUB;
					}
				}
				else
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_SOURCE;
					}
					else
					{
						return BLENDOP_SUB;
					}
				}
			case BLENDOP_INVSUB:
				if(sourceBlendFactorAlpha() == BLEND_ZERO)
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_NULL;
					}
					else
					{
						return BLENDOP_DEST;
					}
				}
				else if(sourceBlendFactorAlpha() == BLEND_ONE)
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_NULL;   // Negative, clamped to zero
					}
					else
					{
						return BLENDOP_INVSUB;
					}
				}
				else
				{
					if(destBlendFactorAlpha() == BLEND_ZERO)
					{
						return BLENDOP_NULL;   // Negative, clamped to zero
					}
					else
					{
						return BLENDOP_INVSUB;
					}
				}
			case BLENDOP_MIN:
				return BLENDOP_MIN;
			case BLENDOP_MAX:
				return BLENDOP_MAX;
			default:
				ASSERT(false);
			}

			return blendOperationStateAlpha;
		}
	}

	bool Context::indexedVertexBlendActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return indexedVertexBlendEnable;
	}

	int Context::vertexBlendMatrixCountActive()
	{
		if(vertexShader)
		{
			return 0;
		}

		return vertexBlendMatrixCount;
	}

	bool Context::localViewerActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return localViewer;
	}

	bool Context::normalizeNormalsActive()
	{
		if(vertexShader)
		{
			return false;
		}

		return normalizeNormals;
	}

	Context::FogMode Context::vertexFogModeActive()
	{
		if(vertexShader || !fogActive())
		{
			return FOG_NONE;
		}

		return vertexFogMode;
	}

	bool Context::rangeFogActive()
	{
		if(vertexShader || !fogActive())
		{
			return false;
		}

		return rangeFogEnable;
	}

	Context::TexGen Context::texGenActive(int stage)
	{
		if(vertexShader || !vertexTextureActive(stage))
		{
			return TEXGEN_PASSTHRU;
		}

		return texGen[stage];
	}
	
	int Context::textureTransformCountActive(int stage)
	{
		if(vertexShader || !vertexTextureActive(stage))
		{
			return 0;
		}

		return textureTransformCount[stage];
	}

	int Context::texCoordIndexActive(int stage)
	{
		if(vertexShader || !vertexTextureActive(stage))
		{
			return stage;
		}

		return textureStage[stage].texCoordIndex;
	}

	bool Context::perspectiveActive()
	{
		if(!colorUsed())
		{
			return false;
		}

		if(!perspectiveCorrection)
		{
			return false;
		}

		if(isDrawPoint(true))
		{
			return false;
		}

		return true;
	}

	bool Context::diffuseUsed()
	{
		return diffuseUsed(0) || diffuseUsed(1) || diffuseUsed(2) || diffuseUsed(3);
	}

	bool Context::diffuseUsed(int component)
	{
		if(!colorUsed())
		{
			return false;
		}

		if(pixelShader)
		{
			return pixelShader->usesDiffuse(component);
		}

		// Directly using the diffuse input color
		for(int i = 0; i < 8; i++)
		{
			if(textureStage[i].isStageDisabled())
			{
				break;
			}

			if(textureStage[i].usesDiffuse())
			{
				return true;
			}
		}

		// Using the current color (initialized to diffuse) before it's overwritten
		for(int i = 0; i < 8; i++)
		{
			if(textureStage[i].usesCurrent() || textureStage[i].isStageDisabled())   // Current color contains diffuse before being overwritten
			{
				return true;
			}

			if(textureStage[i].writesCurrent())
			{
				return false;
			}
		}

		return true;
	}

	bool Context::diffuseActive()
	{
		return diffuseActive(0) || diffuseActive(1) || diffuseActive(2) || diffuseActive(3);
	}

	bool Context::diffuseActive(int component)
	{
		if(!colorUsed())
		{
			return false;
		}

		// Vertex processor provides diffuse component
		bool vertexDiffuse;
		
		if(vertexShader)
		{
			vertexDiffuse = vertexShader->output[D0][component].active();
		}
		else if(!preTransformed)
		{
			vertexDiffuse = input[Color0] || lightingEnable;
		}
		else
		{
			vertexDiffuse = input[Color0];
		}

		// Pixel processor requires diffuse component
		bool pixelDiffuse = diffuseUsed(component);

		return vertexDiffuse && pixelDiffuse;
	}

	bool Context::specularUsed()
	{
		return Context::specularUsed(0) || Context::specularUsed(1) || Context::specularUsed(2) || Context::specularUsed(3);
	}

	bool Context::specularUsed(int component)
	{
		if(!colorUsed())
		{
			return false;
		}

		if(pixelShader)
		{
			return pixelShader->usesSpecular(component);
		}

		bool pixelSpecular = specularEnable;

		for(int i = 0; i < 8; i++)
		{
			if(textureStage[i].isStageDisabled()) break;

			pixelSpecular = pixelSpecular || textureStage[i].usesSpecular();
		}

		return pixelSpecular;
	}

	bool Context::specularActive()
	{
		return specularActive(0) || specularActive(1) || specularActive(2) || specularActive(3);
	}

	bool Context::specularActive(int component)
	{
		if(!colorUsed())
		{
			return false;
		}

		// Vertex processor provides specular component
		bool vertexSpecular;
		
		if(!vertexShader)
		{
			vertexSpecular = input[Color1] || (lightingEnable && specularEnable);
		}
		else
		{
			vertexSpecular = vertexShader->output[D1][component].active();
		}

		// Pixel processor requires specular component
		bool pixelSpecular = specularUsed(component) || fogActive();

		return vertexSpecular && pixelSpecular;
	}

	bool Context::colorActive(int color, int component)
	{
		if(color == 0)
		{
			return diffuseActive(component);
		}
		else
		{
			return specularActive(component);
		}
	}

	bool Context::textureActive()
	{
		for(int i = 0; i < 8; i++)
		{
			if(textureActive(i))
			{
				return true;
			}
		}

		return false;
	}

	bool Context::textureActive(int coordinate)
	{
		return textureActive(coordinate, 0) || textureActive(coordinate, 1) || textureActive(coordinate, 2) || textureActive(coordinate, 3);
	}

	bool Context::textureActive(int coordinate, int component)   // FIXME: Rename to texCoordActive
	{
		if(!colorUsed())
		{
			return false;
		}

		if(!vertexTextureActive(coordinate, component))
		{
			return false;
		}

		if(textureTransformProject[coordinate] && pixelShaderVersion() <= 0x0103)
		{
			if(textureTransformCount[coordinate] == 2)
			{
				if(component == 1) return true;
			}
			else if(textureTransformCount[coordinate] == 3)
			{
				if(component == 2) return true;
			}
			else if(textureTransformCount[coordinate] == 4 || textureTransformCount[coordinate] == 0)
			{
				if(component == 3) return true;
			}
		}

		if(!pixelShader)
		{
			bool texture = textureStage[coordinate].usesTexture();
			bool cube = sampler[coordinate].hasCubeTexture();
			bool volume = sampler[coordinate].hasVolumeTexture();

			if(texture)
			{
				for(int i = coordinate - 1; i >= 0; i--)
				{
					if(textureStage[i].stageOperation == TextureStage::STAGE_DISABLE)
					{
						texture = false;

						break;
					}
				}
			}

			switch(component)
			{
			case 0:
				return texture;
			case 1:
				return texture;
			case 2:
				return (texture && (cube || volume));
			case 3:
				return false;
			}
		}
		else
		{
			return pixelShader->usesTexture(coordinate, component);
		}

		return false;
	}

	unsigned short Context::pixelShaderVersion() const
	{
		return pixelShader ? pixelShader->getVersion() : 0x0000;
	}

	unsigned short Context::vertexShaderVersion() const
	{
		return vertexShader ? vertexShader->getVersion() : 0x0000;
	}

	Format Context::renderTargetInternalFormat(int index)
	{
		if(renderTarget[index])
		{
			return renderTarget[index]->getInternalFormat();
		}
		else
		{
			return FORMAT_NULL;
		}
	}

	int Context::colorWriteActive()
	{
		return colorWriteActive(0) | colorWriteActive(1) | colorWriteActive(2) | colorWriteActive(3);
	}

	int Context::colorWriteActive(int index)
	{
		if(!renderTarget[index] || renderTarget[index]->getInternalFormat() == FORMAT_NULL)
		{
			return 0;
		}

		if(blendOperation() == BLENDOP_DEST && destBlendFactor() == BLEND_ONE &&
		   (!separateAlphaBlendEnable || (blendOperationAlpha() == BLENDOP_DEST && destBlendFactorAlpha() == BLEND_ONE)))
		{
			return 0;
		}

		return colorWriteMask[index];
	}

	bool Context::colorUsed()
	{
		return colorWriteActive() || alphaTestActive() || (pixelShader && pixelShader->containsTexkill());
	}
}