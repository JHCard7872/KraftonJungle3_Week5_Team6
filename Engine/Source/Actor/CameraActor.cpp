#include "CameraActor.h"

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

	AActor::PostSpawnInitialize();
}

void ACameraActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);
	if (this->CameraComponent)
	{
		this->CameraComponent = static_cast<UCameraComponent*>(Context.GetMappedObject(this->CameraComponent));
	}
}