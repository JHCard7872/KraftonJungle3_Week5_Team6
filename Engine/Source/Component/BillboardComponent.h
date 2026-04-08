#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"


struct FDynamicMesh;
class FDynamicMaterial;
struct FMaterialTexture;
class UTexture;
class UMaterial;

class ENGINE_API UBillboardComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UBillboardComponent, UPrimitiveComponent)
	GENERATE_SHALLOW_CLONE(UBillboardComponent);
	~UBillboardComponent() override;
	void PostConstruct() override;

	virtual FBoxSphereBounds GetWorldBounds() const override;

	void SetSize(const FVector2& InSize) { Size = InSize; }
	const FVector2& GetSize() const { return Size; }

	/* 나중에 Proxy 로 뺄 정보 */
	void SetBillboard(bool bInBillboard) { bBillboard = bInBillboard; }
	bool IsBillboard() const { return bBillboard; }

	/** SubUV 렌더링용 메시 데이터 반환 */
	virtual FRenderMesh* GetRenderMesh() const override;
	FDynamicMesh* GetBillboardMesh() const { return BillboardMesh.get(); }
	UMaterial* GetBaseMaterial() const { return BaseMaterial; }

	void SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture);

private:
	FVector2 Size = FVector2(1.f, 1.f);
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	bool bBillboard = false;

	/** 동적 메시 데이터 */
	std::shared_ptr<FDynamicMesh> BillboardMesh;
	UMaterial* BaseMaterial = nullptr;
};

