#pragma once
#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"

//Jsonì„ ë‹´ì•„ë‘ëŠ”ê³³
class ENGINE_API FArchive
{
public:
	FArchive(bool bInSaving);
	~FArchive();
	bool IsSaving() const { return bSaving; }
	bool IsLoading() const { return !bSaving; }
	// ê¸°ë³¸ íƒ€ìž… ì§ë ¬í™”
	void Serialize(const FString& Key, FString& Value);
	void Serialize(const FString& Key, uint32& Value);
	void Serialize(const FString& Key, bool& Value);
	void Serialize(const FString& Key, FVector2& Value);
	void Serialize(const FString& Key, FVector& Value);
	void Serialize(const FString& Key, FVector4& Value);
	// ë°°ì—´
	void Serialize(const FString& Key, TArray<FArchive*>& SubArchives);
	void SerializeUIntArray(const FString& Key, TArray<uint32>& Values);
	void SerializeStringArray(const FString& Key, TArray<FString>& Values);

	// í‚¤ ì¡´ìž¬ ì—¬ë¶€
	bool Contains(const FString& Key) const;
	void* GetRawJson();
private:
	bool bSaving;
	void* JsonData;// nlohmann::json* â€” í—¤ë”ì— json ë…¸ì¶œ ì•ˆ í•¨
};
