#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"

struct FDynamicMesh;
class FDynamicMaterial;
class FArchive;
struct FMaterialTexture;

class ENGINE_API UBillboardComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UBillboardComponent, UPrimitiveComponent)
	GENERATE_SHALLOW_CLONE(UBillboardComponent)

	void PostConstruct() override;
	void Serialize(FArchive& Ar) override;

	virtual FBoxSphereBounds GetWorldBounds() const override;

	void SetSize(const FVector2& InSize) { Size = InSize; }
	const FVector2& GetSize() const { return Size; }

	void SetBillboard(bool bInBillboard) { bBillboard = bInBillboard; }
	bool IsBillboard() const { return bBillboard; }

	void SetEditorOnly(bool bInEditorOnly) { bEditorOnly = bInEditorOnly; }
	bool IsEditorOnly() const { return bEditorOnly; }

	virtual FRenderMesh* GetRenderMesh() const override;
	FDynamicMesh* GetBillboardMesh() const { return BillboardMesh.get(); }
	FDynamicMaterial* GetMaterialInstance() const { return MaterialInstance.get(); }
	const FString& GetSpriteMaterialName() const { return SpriteMaterialName; }

	void SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture);
	bool SetSpriteMaterial(const FString& MaterialName);
	void ResetMaterial(const FString& MaterialName);

private:
	FVector2 Size = FVector2(1.f, 1.f);
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	bool bBillboard = false;
	bool bEditorOnly = false;
	FString SpriteMaterialName;
	std::shared_ptr<FDynamicMesh> BillboardMesh;
	std::shared_ptr<FDynamicMaterial> MaterialInstance;
};
