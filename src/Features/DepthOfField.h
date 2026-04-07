#pragma once

#include "Buffer.h"

struct DepthOfField : Feature
{
public:
	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "Depth of Field"; }
	virtual inline std::string GetShortName() override { return "DepthOfField"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kDisplay; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return std::make_pair(
			"Depth of Field simulates camera lens focus by blurring objects that are "
			"closer to or farther from the focal plane, adding cinematic depth.",
			std::vector<std::string>{
				"Variable-radius Gaussian blur driven by circle of confusion",
				"Separate near-field and far-field scaling",
				"Configurable focus distance and depth-of-field range",
				"Full-resolution separable filter for minimal performance cost" });
	}

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	void CompileComputeShaders();
	bool ShadersOK();
	void DrawDoF();

	//////////////////////////////////////////////////////////////

	bool recompileFlag = false;

	struct Settings
	{
		bool  Enabled       = true;
		float FocusDistance = 3000.0f;  // game units (Skyrim units ≈ cm)
		float FocusRange    = 1500.0f;  // half-width of the in-focus band
		float MaxBlurRadius = 8.0f;     // maximum blur radius in pixels
		float NearBlurScale = 1.0f;
		float FarBlurScale  = 1.0f;
	} settings;

	struct alignas(16) DoFCB
	{
		float FocusDistance;
		float FocusRange;
		float MaxBlurRadius;
		float NearBlurScale;
		float FarBlurScale;
		float _pad0;
		float _pad1;
		float _pad2;
	};
	STATIC_ASSERT_ALIGNAS_16(DoFCB);

	eastl::unique_ptr<ConstantBuffer> dofCB;

	// Intermediate textures (format copied from kMAIN)
	eastl::unique_ptr<Texture2D> texCoC;    // R16_FLOAT – per-pixel CoC
	eastl::unique_ptr<Texture2D> texHBlur;  // matches kMAIN – horizontal-blur result

	winrt::com_ptr<ID3D11ComputeShader> cocCS    = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> hBlurCS  = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> vBlurCS  = nullptr;

	winrt::com_ptr<ID3D11SamplerState> linearClampSampler = nullptr;
};
