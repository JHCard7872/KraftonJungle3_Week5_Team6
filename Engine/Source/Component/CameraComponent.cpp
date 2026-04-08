#include "CameraComponent.h"
#include "Object/Class.h"
#include "Camera/Camera.h"

IMPLEMENT_RTTI(UCameraComponent, USceneComponent)

void UCameraComponent::PostConstruct()
{
	bCanEverTick = true;
	Camera = new FCamera();
}

UCameraComponent::~UCameraComponent()
{
	delete Camera;
	Camera = nullptr;
}

void UCameraComponent::Tick(float DeltaTime)
{
	USceneComponent::Tick(DeltaTime);

	//TODO : will be add CameraArm, shake and interpolation  
}

void UCameraComponent::MoveForward(float Value)
{
	Camera->MoveForward(Value);
}

void UCameraComponent::MoveRight(float Value)
{
	Camera->MoveRight(Value);

}

void UCameraComponent::MoveUp(float Value)
{
	Camera->MoveUp(Value);

}

void UCameraComponent::Rotate(float DeltaYaw, float DeltaPitch)
{
	Camera->Rotate(DeltaYaw, DeltaPitch);
}

FCamera* UCameraComponent::GetCamera() const
{
	return Camera;
}

FMatrix UCameraComponent::GetViewMatrix() const
{
	return Camera->GetViewMatrix();
}

FMatrix UCameraComponent::GetProjectionMatrix() const
{
	return Camera->GetProjectionMatrix();
}

void UCameraComponent::SetFov(float inFov)
{
	Camera->SetFOV(inFov);
}

void UCameraComponent::SetSpeed(float Inspeed)
{
	Camera->SetSpeed(Inspeed);
}

void UCameraComponent::SetSensitivity(float InSetSensitivity)
{
	Camera->SetMouseSensitivity(InSetSensitivity);
}

void UCameraComponent::CopyPropertiesFrom(const UObject* Source)
{
	USceneComponent::CopyPropertiesFrom(Source);
	const UCameraComponent* SourceComp = static_cast<const UCameraComponent*>(Source);

	if (this->Camera && SourceComp->Camera)
	{
		/*this->SetFov(SourceComp->GetCamera()->GetFOV());
		this->SetSpeed(SourceComp->GetCamera()->GetSpeed());
		this->SetSensitivity(SourceComp->GetCamera()->GetMouseSensitivity());*/
		// 위치, 회전을 복사하지 않으면 PIE 카메라가 기본 위치(-5,0,2)에서 시작해
		// 에디터에서 보던 화면과 전혀 다른 시점으로 게임이 시작됨.
		const FCamera* Src = SourceComp->Camera;
		this->Camera->SetPosition(Src->GetPosition());
		this->Camera->SetRotation(Src->GetYaw(), Src->GetPitch());
		this->Camera->SetFOV(Src->GetFOV());
		this->Camera->SetSpeed(Src->GetSpeed());
		this->Camera->SetMouseSensitivity(Src->GetMouseSensitivity());
		this->Camera->SetProjectionMode(Src->GetProjectionMode());
		this->Camera->SetOrthoWidth(Src->GetOrthoWidth());
	}
}
