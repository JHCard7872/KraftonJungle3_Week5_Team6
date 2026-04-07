#include "SkySphereActor.h"

#include "PlaneActor.h"

#include "Asset/ObjManager.h"
#include "Component/SkyComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
 
#include "Object/Class.h"

IMPLEMENT_RTTI(ASkySphereActor, AActor)

void ASkySphereActor::PostSpawnInitialize()
{
	std::filesystem::path SkyPath = FPaths::MeshDir() / "SkySphere.Model";
	UStaticMesh* SkyMesh = FObjManager::LoadModelStaticMeshAsset(FPaths::FromPath(SkyPath));

	SkySphereComponent = FObjectFactory::ConstructObject<USkyComponent>(this, "SkySphereComponent");
	SkySphereComponent->SetStaticMesh(SkyMesh);

	AddOwnedComponent(SkySphereComponent);
	SetRootComponent(SkySphereComponent);

	AActor::PostSpawnInitialize();
}

void ASkySphereActor::FixupReferences(const FDuplicateionContext& Context)
{
	AActor::FixupReferences(Context);
	if (this->SkySphereComponent)
	{
		this->SkySphereComponent = static_cast<UStaticMeshComponent*>(Context.GetMappedObject(this->SkySphereComponent));
	}
}

void ASkySphereActor::CopyPropertiesFrom(const UObject* Source)
{
	AActor::CopyPropertiesFrom(Source);
	const ASkySphereActor* SourceActor = static_cast<const APlaneActor*>(Source);

	this->SkySphereComponent = SourceActor->SkySphereComponent;
}
