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
	void PostConstruct() override;

	virtual FBoxSphereBounds GetWorldBounds() const override;

	void SetSize(const FVector2& InSize) { Size = InSize; }
	const FVector2& GetSize() const { return Size; }

	/* 나중에 Proxy 로 뺄 정보 */
	void SetBillboard(bool bInBillboard) { bBillboard = bInBillboard; }
	bool IsBillboard() const { return bBillboard; }

	/** 에디터 전용 시각화 요소로 표시. PIE(SF_EditorActorVisualization 꺼짐)에서 렌더링 안 됨 */
	void SetEditorOnly(bool bInEditorOnly) { bEditorOnly = bInEditorOnly; }
	bool IsEditorOnly() const { return bEditorOnly; }

	/** SubUV 렌더링용 메시 데이터 반환 */
	virtual FRenderMesh* GetRenderMesh() const override;
	FDynamicMesh* GetBillboardMesh() const { return BillboardMesh.get(); }
	FDynamicMaterial* GetMaterialInstance() const { return MaterialInstance.get(); }
	UMaterial* GetBaseMaterial() const { return BaseMaterial; }

	void SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture);

	/** 등록된 베이스 머티리얼 이름으로 MaterialInstance를 재생성한다.
	 *  투명 PNG 처리 등 다른 머티리얼이 필요할 때 PostConstruct 이후 교체용 */
	void ResetMaterial(const FString& MaterialName);

protected:
	void CopyPropertiesFrom(const UObject* Source) override;

private:
	FVector2 Size = FVector2(1.f, 1.f);
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	bool bBillboard = false;
	bool bEditorOnly = false;

	/** 동적 메시 데이터 */
	std::shared_ptr<FDynamicMesh> BillboardMesh;
	std::shared_ptr<FDynamicMaterial> MaterialInstance;
	UMaterial* BaseMaterial = nullptr;
};

