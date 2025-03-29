#ifndef MATERIAL_H_
#define MATERIAL_H_

#include "core/common.h"

#include "container/array.h"
#include "container/string.h"

#include "vulkan/texture.h"
#include "vulkan/shader.h"
#include "vulkan/pipeline_definition.h"

namespace llt
{
	class SubMesh;

	enum ShaderPassType
	{
		SHADER_PASS_FORWARD,
		SHADER_PASS_SHADOW,
		SHADER_PASS_MAX_ENUM
	};

	struct ShaderPass
	{
		ShaderEffect *shader;
		GraphicsPipelineDefinition pipeline;

		ShaderPass()
			: shader(nullptr)
			, pipeline()
		{
		}
	};

	class Technique
	{
	public:
		Technique() = default;
		~Technique() = default;

		VertexFormat vertexFormat;

		ShaderEffect *passes[SHADER_PASS_MAX_ENUM];
		// todo: default parameters

		bool depthTest = true;
		bool depthWrite = true;
	};

	struct MaterialData
	{
		Vector<TextureView> textures;
		String technique;

//		void *parameters;
//		uint64_t parameterSize;

		MaterialData()
			: textures()
			, technique("UNDEFINED")
//			, parameters(nullptr)
//			, parameterSize(0)
		{
		}

		uint64_t getHash() const
		{
			uint64_t result = 0;

			for (auto &t : textures) {
				hash::combine(&result, &t);
			}

			hash::combine(&result, &technique);

			return result;
		}
	};

	class Material
	{
		friend class MaterialRegistry;

	public:
		Material() = default;
		~Material() = default;

		uint64_t getHash() const;

		const GraphicsPipelineDefinition &getPipelineDef(ShaderPassType pass) const;

		const Vector<BindlessResourceHandle> &getTextures() const;

		const BindlessResourceHandle &getBindlessHandle() const;

	private:
		BindlessResourceHandle m_bindlessHandle;

		Vector<BindlessResourceHandle> m_textures;
//		DynamicShaderBuffer *m_parameterBuffer;
		ShaderPass m_passes[SHADER_PASS_MAX_ENUM];
	};
}

#endif // MATERIAL_H_
