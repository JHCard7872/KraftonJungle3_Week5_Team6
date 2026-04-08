#pragma once
#include "CoreMinimal.h"
#include "Object/Object.h"

class FMaterialTexture;

class UTexture : public UObject
{
public:
	DECLARE_RTTI(UTexture, UObject)

	int32_t GetWidth()  const { return Width; }
	int32_t GetHeight() const { return Height; }

	FMaterialTexture* GetResource() const { return Resource; }

	void UpdateResource();   // GPU resource 생성/갱신
	void ReleaseResource();  // GPU resource 해제

protected:
	int32_t Width = 0;
	int32_t Height = 0;

	FMaterialTexture* Resource = nullptr;
};