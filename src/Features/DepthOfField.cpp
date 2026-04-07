#include "DepthOfField.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DepthOfField::Settings,
	Enabled,
	FocusDistance,
	FocusRange,
	MaxBlurRadius,
	NearBlurScale,
	FarBlurScale)

////////////////////////////////////////////////////////////////////////////////////

void DepthOfField::RestoreDefaultSettings()
{
	settings = {};
}

void DepthOfField::DrawSettings()
{
	if (!ShadersOK())
		ImGui::TextColored({ 1, 0, 0, 1 }, "Compute shaders failed to compile!");

	ImGui::Checkbox("Enabled", &settings.Enabled);

	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Enable depth-of-field blur. Objects outside the focal band are blurred.");

	if (!settings.Enabled)
		return;

	ImGui::Separator();

	ImGui::SliderFloat("Focus Distance", &settings.FocusDistance, 100.0f, 20000.0f, "%.0f units");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("View-space distance to the centre of the focal plane (Skyrim units ≈ cm).");

	ImGui::SliderFloat("Focus Range", &settings.FocusRange, 100.0f, 10000.0f, "%.0f units");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Half-width of the in-focus band around the focal plane. Smaller values give a shallower depth of field.");

	ImGui::SliderFloat("Max Blur Radius", &settings.MaxBlurRadius, 1.0f, 16.0f, "%.1f px");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Maximum blur radius in pixels applied at full defocus.");

	ImGui::Separator();

	ImGui::SliderFloat("Near Blur Scale", &settings.NearBlurScale, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier for blur on objects in front of the focal plane.");

	ImGui::SliderFloat("Far Blur Scale", &settings.FarBlurScale, 0.0f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Multiplier for blur on objects behind the focal plane.");
}

void DepthOfField::LoadSettings(json& o_json)
{
	settings = o_json;
}

void DepthOfField::SaveSettings(json& o_json)
{
	o_json = settings;
}

////////////////////////////////////////////////////////////////////////////////////

bool DepthOfField::ShadersOK()
{
	return cocCS && hBlurCS && vBlurCS;
}

void DepthOfField::CompileComputeShaders()
{
	struct ShaderInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* ptr;
		std::wstring                         filename;
	};

	std::array shaders{
		ShaderInfo{ &cocCS,   L"Data\\Shaders\\DepthOfField\\coc.cs.hlsl"   },
		ShaderInfo{ &hBlurCS, L"Data\\Shaders\\DepthOfField\\hblur.cs.hlsl" },
		ShaderInfo{ &vBlurCS, L"Data\\Shaders\\DepthOfField\\vblur.cs.hlsl" },
	};

	for (auto& s : shaders) {
		if (auto* raw = reinterpret_cast<ID3D11ComputeShader*>(
				Util::CompileShader(s.filename.c_str(), {}, "cs_5_0")))
			s.ptr->attach(raw);
	}
}

void DepthOfField::SetupResources()
{
	dofCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<DoFCB>());

	auto renderer = globals::game::renderer;
	auto device   = globals::d3d::device;

	// ----- CoC texture: R16_FLOAT, same resolution as screen -----
	{
		auto mainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		mainRT.texture->GetDesc(&texDesc);
		texDesc.Format    = DXGI_FORMAT_R16_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = 1;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{
			.Format        = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D     = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{
			.Format        = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D     = { .MipSlice = 0 }
		};

		texCoC = eastl::make_unique<Texture2D>(texDesc);
		texCoC->CreateSRV(srvDesc);
		texCoC->CreateUAV(uavDesc);
	}

	// ----- H-blur intermediate: same format and size as kMAIN -----
	{
		auto mainRT = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		mainRT.texture->GetDesc(&texDesc);
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		mainRT.SRV->GetDesc(&srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		mainRT.UAV->GetDesc(&uavDesc);

		texHBlur = eastl::make_unique<Texture2D>(texDesc);
		texHBlur->CreateSRV(srvDesc);
		texHBlur->CreateUAV(uavDesc);
	}

	// ----- Linear clamp sampler -----
	{
		D3D11_SAMPLER_DESC samplerDesc{
			.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MipLODBias     = 0.0f,
			.MaxAnisotropy  = 1,
			.ComparisonFunc = D3D11_COMPARISON_NEVER,
			.MinLOD         = 0.0f,
			.MaxLOD         = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linearClampSampler.put()));
	}

	CompileComputeShaders();
}

void DepthOfField::ClearShaderCache()
{
	cocCS   = nullptr;
	hBlurCS = nullptr;
	vBlurCS = nullptr;

	CompileComputeShaders();
}

////////////////////////////////////////////////////////////////////////////////////

void DepthOfField::DrawDoF()
{
	if (!settings.Enabled || !ShadersOK())
		return;

	ZoneScoped;
	TracyD3D11Zone(globals::state->tracyCtx, "Depth of Field");

	if (globals::state->frameAnnotations)
		globals::state->BeginPerfEvent("Depth of Field");

	auto context  = globals::d3d::context;
	auto renderer = globals::game::renderer;

	auto main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	// Update constant buffer
	{
		DoFCB cb{};
		cb.FocusDistance = settings.FocusDistance;
		cb.FocusRange    = settings.FocusRange;
		cb.MaxBlurRadius = settings.MaxBlurRadius;
		cb.NearBlurScale = settings.NearBlurScale;
		cb.FarBlurScale  = settings.FarBlurScale;
		dofCB->Update(cb);
	}

	auto  cbPtr          = dofCB->CB();
	auto* sharedDataBuf  = globals::state->sharedDataCB->CB();
	auto  dispatchCount  = Util::GetScreenDispatchCount(true);

	context->CSSetConstantBuffers(1, 1, &cbPtr);
	context->CSSetConstantBuffers(5, 1, &sharedDataBuf);

	auto* depthSRV = Util::GetCurrentSceneDepthSRV();

	// ------------------------------------------------------------------
	// Pass 1: CoC  (depth → CoCTex)
	// ------------------------------------------------------------------
	{
		ID3D11ShaderResourceView*  srvs[1]{ depthSRV };
		ID3D11UnorderedAccessView* uavs[1]{ texCoC->uav.get() };

		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(cocCS.get(), nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	// ------------------------------------------------------------------
	// Pass 2: Horizontal blur  (kMAIN + CoC → texHBlur)
	// ------------------------------------------------------------------
	{
		ID3D11ShaderResourceView*  srvs[2]{ main.SRV, texCoC->srv.get() };
		ID3D11UnorderedAccessView* uavs[1]{ texHBlur->uav.get() };

		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(hBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	// ------------------------------------------------------------------
	// Pass 3: Vertical blur + composite  (texHBlur + kMAIN + CoC → kMAIN)
	// ------------------------------------------------------------------
	{
		ID3D11ShaderResourceView*  srvs[3]{ texHBlur->srv.get(), main.SRV, texCoC->srv.get() };
		ID3D11UnorderedAccessView* uavs[1]{ main.UAV };

		context->CSSetShaderResources(0, 3, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetShader(vBlurCS.get(), nullptr, 0);
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
	}

	// ------------------------------------------------------------------
	// Cleanup
	// ------------------------------------------------------------------
	{
		ID3D11ShaderResourceView*  nullSRVs[3]{};
		ID3D11UnorderedAccessView* nullUAVs[1]{};
		ID3D11Buffer*              nullBuf = nullptr;

		context->CSSetShaderResources(0, 3, nullSRVs);
		context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		context->CSSetConstantBuffers(1, 1, &nullBuf);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (globals::state->frameAnnotations)
		globals::state->EndPerfEvent();
}
