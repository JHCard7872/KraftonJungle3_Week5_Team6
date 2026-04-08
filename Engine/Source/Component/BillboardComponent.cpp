#include "BillboardComponent.h"
#include "PrimitiveComponent.h"
#include "Renderer/MeshData.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Object/Object.h"
#include "Object/Class.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_RTTI(UBillboardComponent, UPrimitiveComponent)

UBillboardComponent::~UBillboardComponent()
{
	if (BaseMaterial)
	{
		BaseMaterial->MarkPendingKill();
		BaseMaterial = nullptr;
	}
}

void UBillboardComponent::PostConstruct()
{
	bDrawDebugBounds = false;
	bBillboard = true;
	BillboardMesh = std::make_shared<FDynamicMesh>();

	BaseMaterial = FObjectFactory::ConstructObject<UMaterial>(this, GetName() + "_Mat");
	BaseMaterial->SetRenderMaterial(FMaterialManager::Get().FindByName("M_Default_Texture")->CreateDynamicMaterial());
}

FBoxSphereBounds UBillboardComponent::GetWorldBounds() const
{
	const FVector Center = GetWorldLocation();
	const FVector WorldScale = GetWorldTransform().GetScaleVector();

	const float HalfW = Size.X * 0.5f * WorldScale.X;
	const float HalfH = Size.Y * 0.5f * WorldScale.Y;
	const float HalfZ = ((HalfW > HalfH) ? HalfW : HalfH);

	const FVector BoxExtent(HalfW, HalfH, HalfZ);
	return { Center, BoxExtent.Size(), BoxExtent };
}

FRenderMesh* UBillboardComponent::GetRenderMesh() const
{
	return BillboardMesh.get();
}

void UBillboardComponent::SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture)
{
	if (BaseMaterial && BaseMaterial->GetRenderMaterial())
	{
		BaseMaterial->SetDiffuse(InTexture.get());
		BaseMaterial->GetRenderMaterial()->SetMaterialTexture(InTexture);
	}
}

void UBillboardComponent::CopyPropertiesFrom(const UObject* Source)
{
	UPrimitiveComponent::CopyPropertiesFrom(Source);
	const UBillboardComponent* SourceComp = static_cast<const UBillboardComponent*>(Source);

	this->Size = SourceComp->Size;
	this->Color = SourceComp->Color;
	this->bBillboard = SourceComp->bBillboard;

	this->BillboardMesh = SourceComp->BillboardMesh;
	this->BaseMaterial = SourceComp->BaseMaterial;
}
