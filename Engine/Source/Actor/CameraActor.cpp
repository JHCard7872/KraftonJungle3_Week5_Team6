#include "CameraActor.h"
#include "Component/BillboardComponent.h"
#include "Component/CameraArrowComponent.h"
#include "Primitive/PrimitiveGizmo.h"
#include "Core/Engine.h"
#include "Core/Paths.h"
#include "Renderer/Renderer.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Component/CameraComponent.h"
#include "Object/ObjectFactory.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(ACameraActor, AActor)

void ACameraActor::PostSpawnInitialize()
{
	// UCameraComponent는 USceneComponent를 상속하므로 루트 컴포넌트로 직접 사용 가능.
	CameraComponent = FObjectFactory::ConstructObject<UCameraComponent>(this, "CameraComponent");
	AddOwnedComponent(CameraComponent);
	SetRootComponent(CameraComponent);	

	// 카메라 아이콘 빌보드 (Editor/Icon/PlayWorld.png)
	IconBillboard = FObjectFactory::ConstructObject<UBillboardComponent>(this, "CameraIconBillboard");
	AddOwnedComponent(IconBillboard);
	IconBillboard->AttachTo(CameraComponent);
	IconBillboard->SetSize(FVector2(0.5f, 0.5f));
	IconBillboard->SetEditorOnly(true); // PIE에서 렌더링 안 됨

	// 투명 PNG 지원을 위해 알파 블렌딩 머티리얼로 교체 (M_Default_Texture → M_BillboardIcon)
	IconBillboard->ResetMaterial("M_BillboardIcon");

	if (GEngine)
	{
		if (FRenderer* Renderer = GEngine->GetRenderer())
		{
			std::filesystem::path TexPath = FPaths::ProjectRoot() / "Editor/Icon/PlayWorld.png";
			ID3D11ShaderResourceView* SRV = nullptr;
			if (Renderer->CreateTextureFromSTB(Renderer->GetDevice(), TexPath, &SRV))
			{
				auto MatTex = std::make_shared<FMaterialTexture>();
				MatTex->TextureSRV = SRV;
				IconBillboard->SetSpriteTexture(MatTex);
			}
		}
	}

	// 카메라 방향 화살표 (Gizmo Translation Axis Mesh 재사용)
	// 기즈모 축 길이(35 units)를 월드에서 약 1.4 units로 보이도록 축소
	constexpr float ArrowScale = 0.02f;
	FTransform ArrowTransform;
	ArrowTransform.SetScale3D(FVector(ArrowScale, ArrowScale, ArrowScale));

	ArrowX = FObjectFactory::ConstructObject<UCameraArrowComponent>(this, "CameraArrowX");
	AddOwnedComponent(ArrowX);
	ArrowX->AttachTo(CameraComponent);
	ArrowX->SetRelativeTransform(ArrowTransform);
	ArrowX->SetArrowMesh(FPrimitiveGizmo::CreateTranslationAxisMesh(EAxis::X, FVector4(0.67f, 0.90f, 1.0f, 1.0f)));

	AActor::PostSpawnInitialize();
}

void ACameraActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);
	if (this->CameraComponent)
		this->CameraComponent = static_cast<UCameraComponent*>(Context.GetMappedObject(this->CameraComponent));
	if (this->IconBillboard)
		this->IconBillboard = static_cast<UBillboardComponent*>(Context.GetMappedObject(this->IconBillboard));
	if (this->ArrowX)
		this->ArrowX = static_cast<UCameraArrowComponent*>(Context.GetMappedObject(this->ArrowX));
}
void ACameraActor::CopyPropertiesFrom(const UObject* Source)
{
	AActor::CopyPropertiesFrom(Source);
	const ACameraActor* SourceActor = static_cast<const ACameraActor*> (Source);

	// 얕은 복사 -> FixupReferences에서 PIE 월드 내 복사본으로 교정됨.
	this->CameraComponent = SourceActor->CameraComponent;
	this->IconBillboard = SourceActor->IconBillboard;
	this->ArrowX = SourceActor->ArrowX;
}