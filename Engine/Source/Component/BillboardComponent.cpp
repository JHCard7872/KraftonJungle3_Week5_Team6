#include "BillboardComponent.h"
#include "PrimitiveComponent.h"
#include "Renderer/MeshData.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Object/Object.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UBillboardComponent, UPrimitiveComponent)

void UBillboardComponent::PostConstruct()
{
	bDrawDebugBounds = false;
	bBillboard = true;
	BillboardMesh = std::make_shared<FDynamicMesh>();

	MaterialInstance = FMaterialManager::Get().FindByName("M_Default_Texture")->CreateDynamicMaterial();
}

void UBillboardComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	Ar.Serialize("Size", Size);
	Ar.Serialize("Billboard", bBillboard);
	Ar.Serialize("EditorOnly", bEditorOnly);
	Ar.Serialize("SpriteMaterial", SpriteMaterialName);

	if (Ar.IsLoading() && !SpriteMaterialName.empty())
	{
		SetSpriteMaterial(SpriteMaterialName);
	}
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

void UBillboardComponent::FixupReferences(const FDuplicateionContext& Context)
{
	UPrimitiveComponent::FixupReferences(Context);

	// 2. 포인터 공유 방지 (PIE 복제본에게만 새 리소스 발급)
	this->BillboardMesh = std::make_shared<FDynamicMesh>();
	if (this->MaterialInstance)
	{
		this->MaterialInstance = this->MaterialInstance->CreateDynamicMaterial();
	}
}

void UBillboardComponent::SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture)
{
	if (MaterialInstance)
	{
		MaterialInstance->SetMaterialTexture(InTexture);
	}
}

bool UBillboardComponent::SetSpriteMaterial(const FString& MaterialName)
{
	const std::shared_ptr<FMaterial> Material = FMaterialManager::Get().FindByName(MaterialName);
	if (!Material || !Material->GetMaterialTexture())
	{
		return false;
	}

	if (!MaterialInstance)
	{
		const std::shared_ptr<FMaterial> DefaultMaterial = FMaterialManager::Get().FindByName("M_Default_Texture");
		if (DefaultMaterial)
		{
			MaterialInstance = DefaultMaterial->CreateDynamicMaterial();
		}
	}

	if (!MaterialInstance)
	{
		return false;
	}

	MaterialInstance->SetMaterialTexture(Material->GetMaterialTexture());
	SpriteMaterialName = Material->GetOriginName();
	return true;
}

void UBillboardComponent::ResetMaterial(const FString& MaterialName)
{
	auto Base = FMaterialManager::Get().FindByName(MaterialName);
	if (Base)
	{
		MaterialInstance = Base->CreateDynamicMaterial();
	}
}
