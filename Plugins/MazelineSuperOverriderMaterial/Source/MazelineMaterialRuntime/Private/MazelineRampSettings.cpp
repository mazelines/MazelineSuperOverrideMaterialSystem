#include "MazelineRampSettings.h"
#include "SceneViewExtension.h"
#include "SceneRendering.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "TextureResource.h"
#include "Curves/CurveLinearColorAtlas.h"

class FMazelineRampAtlasCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FMazelineRampAtlasCS);
    SHADER_USE_PARAMETER_STRUCT(FMazelineRampAtlasCS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_TEXTURE(Texture2D, InputAtlas)
        SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
        SHADER_PARAMETER(FIntPoint, InputSize)
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(uint32, Slice)
        SHADER_PARAMETER(uint32, Enabled)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, OutputAtlas)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FMazelineRampAtlasCS, "/MazelineMaterial/Private/MazelineRampAtlas.usf", "MainCS", SF_Compute);

namespace
{
FTextureReferenceRHIRef AtlasReferences[3]; // Render-thread ownership.
struct FMazelineRampFrame
{
    TRefCountPtr<IPooledRenderTarget> Atlas;
    TArray<const FSceneView*> Views;
};
}
RDG_REGISTER_BLACKBOARD_STRUCT(FMazelineRampFrame);

class FMazelineRampViewExtension : public FSceneViewExtensionBase
{
public:
    FMazelineRampViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}
    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&, FSceneView&) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}

    virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& Family) override
    {
        // The unused native Toon Profile slot is available only with Substrate disabled.
        if (Substrate::IsSubstrateEnabled() || Family.GetFeatureLevel() < ERHIFeatureLevel::SM5) return;
        auto& Frame = GraphBuilder.Blackboard.GetOrCreate<FMazelineRampFrame>();
        for (const FSceneView* View : Family.Views) Frame.Views.AddUnique(View);
        if (Frame.Atlas) return;
        FRHITexture* Inputs[3] = {};
        FIntPoint Size(2, 1);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Inputs[Index] = AtlasReferences[Index] ? AtlasReferences[Index]->GetReferencedTexture() : nullptr;
            if (Inputs[Index])
            {
                Size.X = FMath::Max(Size.X, Inputs[Index]->GetDesc().Extent.X);
                Size.Y = FMath::Max(Size.Y, Inputs[Index]->GetDesc().Extent.Y);
            }
        }
        const auto Desc = FRDGTextureDesc::Create2DArray(Size, PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV, 3);
        FRDGTextureRef Atlas = GraphBuilder.CreateTexture(Desc, TEXT("MazelineDirectionalRampAtlas"));
        Frame.Atlas = GraphBuilder.ConvertToExternalTexture(Atlas);
        TShaderMapRef<FMazelineRampAtlasCS> Shader(GetGlobalShaderMap(Family.GetFeatureLevel()));
        for (uint32 Index = 0; Index < 3; ++Index)
        {
            auto* Parameters = GraphBuilder.AllocParameters<FMazelineRampAtlasCS::FParameters>();
            Parameters->InputAtlas = Inputs[Index] ? Inputs[Index] : GWhiteTexture->TextureRHI.GetReference();
            Parameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
            Parameters->InputSize = Parameters->InputAtlas->GetDesc().Extent;
            Parameters->OutputSize = Size;
            Parameters->Slice = Index;
            Parameters->Enabled = Inputs[Index] ? 1 : 0;
            Parameters->OutputAtlas = GraphBuilder.CreateUAV(Atlas);
            FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Mazeline Ramp LUT %u", Index + 1), Shader, Parameters,
                FIntVector(FMath::DivideAndRoundUp(Size.X, 8), FMath::DivideAndRoundUp(Size.Y, 8), 1));
        }
        FRDGExternalAccessQueue Queue;
        Queue.Add(Atlas);
        Queue.Submit(GraphBuilder);
    }

    virtual void PreRenderBasePass_RenderThread(FRDGBuilder& GraphBuilder, bool) override
    {
        auto* Frame = GraphBuilder.Blackboard.Get<FMazelineRampFrame>();
        if (!Frame || !Frame->Atlas || Substrate::IsSubstrateEnabled()) return;
        FRHITexture* Atlas = Frame->Atlas->GetRHI();
        const FIntPoint Size = Atlas->GetDesc().Extent;
        for (const FSceneView* BaseView : Frame->Views)
        {
            if (!BaseView->bIsViewInfo) continue;
            FViewInfo& View = *static_cast<FViewInfo*>(const_cast<FSceneView*>(BaseView));
            if (!View.CachedViewUniformShaderParameters) continue;
            auto& Parameters = *View.CachedViewUniformShaderParameters;
            Parameters.ToonProfileTexture = Atlas;
            Parameters.ToonProfileSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
            Parameters.ToonProfileTextureSizeAndInvSize = FVector4f(Size.X, Size.Y, 1.f / Size.X, 1.f / Size.Y);
            // Create a new immutable buffer before base/deferred pass parameters are built.
            // No in-flight uniform buffer is mutated, and other View fields are preserved.
            View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleFrame);
        }
    }
};

void UMazelineRampSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (!IsRunningCommandlet()) ReloadAtlases();
}

void UMazelineRampSubsystem::ReloadAtlases()
{
    check(IsInGameThread());
    if (IsRunningCommandlet()) return;
    const auto* Settings = GetDefault<UMazelineRampSettings>();
    LoadedAtlases.SetNum(3);
    TArray<FTextureReferenceRHIRef> References;
    References.SetNum(3);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const auto* Path = Settings->AtlasesByFunction.Find(Index + 1);
        auto* Atlas = Path ? Cast<UCurveLinearColorAtlas>(Path->LoadSynchronous()) : nullptr;
        LoadedAtlases[Index] = Atlas;
        if (Atlas)
        {
            References[Index] = Atlas->TextureReference.TextureReferenceRHI;
            UE_LOG(LogTemp, Display, TEXT("MAZELINE_RAMP_ATLAS function=%d texture=%s"), Index + 1, *Atlas->GetPathName());
        }
        else if (Path && !Path->IsNull()) UE_LOG(LogTemp, Error, TEXT("MAZELINE_RAMP_ATLAS missing function=%d texture=%s"), Index + 1, *Path->ToString());
    }
    ENQUEUE_RENDER_COMMAND(MazelinePublishRampAtlases)([References = MoveTemp(References)](FRHICommandListImmediate&)
    {
        for (int32 Index = 0; Index < 3; ++Index) AtlasReferences[Index] = References[Index];
    });
    if (!ViewExtension) ViewExtension = FSceneViewExtensions::NewExtension<FMazelineRampViewExtension>();
}

void UMazelineRampSubsystem::Deinitialize()
{
    ViewExtension.Reset();
    ENQUEUE_RENDER_COMMAND(MazelineReleaseRampAtlases)([](FRHICommandListImmediate&) { for (auto& Reference : AtlasReferences) Reference.SafeRelease(); });
    LoadedAtlases.Reset();
    Super::Deinitialize();
}
