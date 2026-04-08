#include "EditorEngine.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "Actor/Actor.h"
#include "Actor/CameraActor.h"
#include "Camera/Camera.h"
#include "Component/CameraComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/ConsoleVariableManager.h"
#include "Core/Engine.h"
#include "Input/InputManager.h"
#include "Debug/EngineLog.h"
#include "Input/InputManager.h"
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Scene/Level.h"
#include "Viewport/Viewport.h"
#include "Viewport/EditorViewportClient.h"
#include "Viewport/PreviewViewportClient.h"
#include "World/World.h"
#include "Slate/EditorViewportOverlay.h"

namespace
{
	constexpr const char* PreviewLevelContextName = "PreviewLevel";

	const TArray<FWorldContext*>& GetEmptyPreviewWorldContexts()
	{
		static TArray<FWorldContext*> EmptyPreviewWorldContexts;
		return EmptyPreviewWorldContexts;
	}

	void InitializeDefaultPreviewLevel(FEditorEngine* Engine)
	{
		if (Engine == nullptr)
		{
			return;
		}

		FWorldContext* PreviewContext = Engine->CreatePreviewWorldContext(PreviewLevelContextName, 1280, 720);
		if (PreviewContext == nullptr || PreviewContext->World == nullptr)
		{
			return;
		}

		UWorld* PreviewWorld = PreviewContext->World;
		if (PreviewWorld->GetActors().empty())
		{
			/*AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>("PreviewCube");
			if (PreviewActor)
			{
				UStaticMeshComponent* PreviewComponent = FObjectFactory::ConstructObject<UStaticMeshComponent>(PreviewActor);
				PreviewActor->AddOwnedComponent(PreviewComponent);
				PreviewActor->SetRootComponent(PreviewComponent);

				PreviewComponent->SetStaticMesh(FObjManager::GetPrimitiveCube());
				PreviewActor->SetActorLocation({ 0.0f, 0.0f, 0.0f });
			}*/
		}

		if (UCameraComponent* PreviewCamera = PreviewWorld->GetActiveCameraComponent())
		{
			PreviewCamera->GetCamera()->SetPosition({ -8.0f, -8.0f, 6.0f });
			PreviewCamera->GetCamera()->SetRotation(45.0f, -20.0f);
			PreviewCamera->SetFov(50.0f);
		}
	}
}

FEditorEngine::~FEditorEngine() = default;

void FEditorEngine::StartPIE()
{
	if (IsPlayingInEditor() || !EditorWorldContext || !EditorWorldContext->World) return;

	UE_LOG("[PIE] Play In Editor Started. Click Viewport to capture mouse.");

	// PIE 시작 시 에디터 카메라 상태 백업
	EditorCameraStatesBackup.clear();
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		EditorCameraStatesBackup[Entry.Id] = Entry.LocalState;
	}

	// 현재 에디터 카메라 정보 저장
	FVector EditorCamPos;
	float EditorCamYaw = 0.0f;
	float EditorCamPitch = 0.0f;
	if (UCameraComponent* EditorCam = EditorWorldContext->World->GetActiveCameraComponent())
	{
		EditorCamPos = EditorCam->GetCamera()->GetPosition();
		EditorCamYaw = EditorCam->GetCamera()->GetYaw();
		EditorCamPitch = EditorCam->GetCamera()->GetPitch();
	}

	// PIE 진입 전 에디터 선택을 해제한다.
	// 선택된 채로 PIE에 진입하면 에디터 액터를 가리키는 SelectionSubsystem 포인터가
	// PIE 중 기즈모 드래그로 에디터 액터를 직접 이동시키는 문제가 발생한다.
	SetSelectedActor(nullptr);

	FDuplicateionContext Context;
	UWorld* EditorWorld = EditorWorldContext->World;

	UWorld* PIEWorld = static_cast<UWorld*>(EditorWorld->Duplicate(Context, nullptr));
	// Duplicate 끝난 뒤 Context에 등록된 모든 복사본에 FixupReferences 호출해야 함.
	// 루트(PIEWorld)만 호출하면 Actor::RootComponent, ActorComponent::Owner,
	// SceneComponent::AttachParent/Children 등 Actor, Component 계층의 포인터는
	// 여전히 EditorWorld의 오브젝트를 가리킨 채로 남음.
	for (auto& [OldObj, NewObj] : Context.DuplicatedObjects)
	{
		//PIEWorld->FixupReferences(Context);
		NewObj->FixupReferences(Context);
	}

	// 레벨에 ACameraActor가 있으면 그 컴포넌트를 PIE 월드의 활성 카메라로 설정함.
	// UE와 동일하게 배치된 카메라 액터의 시점으로 PIE가 시작됨.
	if (ULevel* PIELevel = PIEWorld->GetLevel())
	{
		for (AActor* Actor : PIELevel->GetActors())
		{
			if (Actor && Actor->IsA(ACameraActor::StaticClass()))
			{
				ACameraActor* CameraActor = static_cast<ACameraActor*>(Actor);
				if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
				{
					PIEWorld->SetActiveCameraComponent(CameraComponent);
					UE_LOG("[PIE] Camera possession: ACameraActor");
				}
				break;
			}
		}
	}

	float AspectRatio = MainWindow ? (static_cast<float>(MainWindow->GetWidth()) / static_cast<float>(MainWindow->GetHeight())) : 1.0f;

	PIEWorldContext = CreateWorldContext("PIELevel", EWorldType::PIE, AspectRatio, false);

	if (PIEWorldContext->World && PIEWorldContext->World != PIEWorld)
	{
		PIEWorldContext->World = PIEWorld;
	}

	// CopyPropertiesFrom이 EditorWorld의 WorldType(= EWorldType::Editor)을 복사하므로
	// 명시적으로 PIE 타입으로 덮어써야 한다.
	// 이 값이 Editor인 채로 남으면 FEditorCameraSubsystem::SyncActiveCamera가
	// IsEditorLevel() == true로 판정해 매 프레임 PIE 카메라를 에디터 카메라로 덮어쓴다.
	PIEWorld->SetWorldType(EWorldType::PIE);
	
	ActiveEditorWorldContext = PIEWorldContext;

	// PIE 월드의 카메라를 에디터 카메라 위치로 동기화
	if (UCameraComponent* PIECam = PIEWorld->GetActiveCameraComponent())
	{
		PIECam->GetCamera()->SetPosition(EditorCamPos);
		PIECam->GetCamera()->SetRotation(EditorCamYaw, EditorCamPitch);
	}

	PIEWorld->BeginPlay();
	SyncViewportClient();

	// 마우스 캡처는 뷰포트 클릭 시 발생하도록 유도 (언리얼 스타일)
}

void FEditorEngine::EndPIE()
{
	if (!IsPlayingInEditor()) return;

	UE_LOG("[PIE] Play In Editor Stopped.");

	// 마우스 캡처 해제
	if (FInputManager* Input = GetInputManager())
	{
		Input->SetMouseCapture(false);
	}

	if (PIEWorldContext && PIEWorldContext->World)
	{
		// PIEWorldContext->World->EndPlay();
		// PIEWorldContext->World->Destroy();
	}

	ActiveEditorWorldContext = EditorWorldContext;

	if (PIEWorldContext)
	{
		DestroyWorldContext(PIEWorldContext);
		PIEWorldContext = nullptr;
	}
	
	// PIE 종료 시 선택을 해제한다. PIE 월드 액터 포인터가 에디터 서브시스템에 남으면
	// DestroyWorldContext 이후 댕글링 포인터가 될 수 있다.
	SetSelectedActor(nullptr);

	// PIE 전용 뷰포트 클라이언트를 해제한다.
	// reset() 전에 먼저 ActiveViewportClient를 nullptr로 교체해
	// 해제된 포인터에 Detach가 호출되지 않도록 한다.
	// SetViewportClient(nullptr)이 살아 있는 PIEViewportClient 위에서 Detach를 호출한 뒤
	// ActiveViewportClient를 nullptr로 만들어 주므로 안전하다.
	SetViewportClient(nullptr);
	PIEViewportClient.reset();

	// PIE 종료 시 에디터 카메라 상태 복구
	for (auto& Pair : EditorCameraStatesBackup)
	{
		if (FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(Pair.first))
		{
			Entry->LocalState = Pair.second;
		}
	}
	EditorCameraStatesBackup.clear();

	SyncViewportClient();
	SyncFocusedViewportLocalState();
}

void FEditorEngine::Shutdown()
{
	FEngineLog::Get().SetCallback({});
	EditorUI.SaveEditorSettings();

	if (GetViewportClient() == PreviewViewportClient.get() ||
		GetViewportClient() == PIEViewportClient.get())
	{
		SetViewportClient(nullptr);
	}

	PIEViewportClient.reset();
	PreviewViewportClient.reset();
	CameraSubsystem.Shutdown();
	SelectionSubsystem.Shutdown();
	ReleaseEditorWorlds();

	FEngine::Shutdown();
}

void FEditorEngine::SetSelectedActor(AActor* InActor)
{
	SelectionSubsystem.SetSelectedActor(InActor);
}

AActor* FEditorEngine::GetSelectedActor() const
{
	return SelectionSubsystem.GetSelectedActor();
}

void FEditorEngine::ActivateEditorLevel()
{
	ActiveEditorWorldContext = (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext : nullptr;
}

bool FEditorEngine::ActivatePreviewLevel(const FString& ContextName)
{
	FWorldContext* PreviewContext = FindPreviewWorld(ContextName);
	if (PreviewContext == nullptr)
	{
		return false;
	}

	ActiveEditorWorldContext = PreviewContext;
	return true;
}

ULevel* FEditorEngine::GetEditorLevel() const
{
	return (EditorWorldContext && EditorWorldContext->World) ? EditorWorldContext->World->GetLevel() : nullptr;
}

ULevel* FEditorEngine::GetPreviewLevel(const FString& ContextName) const
{
	const FWorldContext* Context = FindPreviewWorld(ContextName);
	return (Context && Context->World) ? Context->World->GetLevel() : nullptr;
}

UWorld* FEditorEngine::GetEditorWorld() const
{
	return EditorWorldContext ? EditorWorldContext->World : nullptr;
}

const TArray<FWorldContext*>& FEditorEngine::GetPreviewWorldContexts() const
{
	return PreviewWorldContexts.empty() ? GetEmptyPreviewWorldContexts() : PreviewWorldContexts;
}

FWorldContext* FEditorEngine::CreatePreviewWorldContext(const FString& ContextName, int32 Width, int32 Height)
{
	if (ContextName.empty())
	{
		return nullptr;
	}

	if (FWorldContext* ExistingContext = FindPreviewWorld(ContextName))
	{
		return ExistingContext;
	}

	const float AspectRatio = (Height > 0) ? (static_cast<float>(Width) / static_cast<float>(Height)) : 1.0f;
	FWorldContext* PreviewContext = CreateWorldContext(ContextName, EWorldType::Preview, AspectRatio, false);
	if (!PreviewContext)
	{
		return nullptr;
	}

	PreviewWorldContexts.push_back(PreviewContext);
	return PreviewContext;
}

ULevel* FEditorEngine::GetLevel() const
{
	return GetActiveLevel();
}

ULevel* FEditorEngine::GetActiveLevel() const
{
	UWorld* ActiveWorld = GetActiveWorld();
	return ActiveWorld ? ActiveWorld->GetLevel() : nullptr;
}

UWorld* FEditorEngine::GetActiveWorld() const
{
	return ActiveEditorWorldContext ? ActiveEditorWorldContext->World : FEngine::GetActiveWorld();
}

const FWorldContext* FEditorEngine::GetActiveWorldContext() const
{
	return ActiveEditorWorldContext ? ActiveEditorWorldContext : FEngine::GetActiveWorldContext();
}

void FEditorEngine::HandleResize(int32 Width, int32 Height)
{
	FEngine::HandleResize(Width, Height);

	if (Width == 0 || Height == 0)
	{
		return;
	}

	UpdateEditorWorldAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
}

void FEditorEngine::PreInitialize()
{
	// 에디터 UI가 올라오기 전에 DPI와 로그 연결만 먼저 준비한다.
	ImGui_ImplWin32_EnableDpiAwareness();

	FEngineLog::Get().SetCallback([this](const char* Msg)
	{
		EditorUI.GetConsole().AddLog("%s", Msg);
	});
}

void FEditorEngine::BindHost(FWindowsWindow* InMainWindow)
{
	// 실제 UI/뷰포트 생성은 뒤 단계에서 하고, 여기서는 창 참조만 저장한다.
	MainWindow = InMainWindow;
	EditorUI.SetupWindow(InMainWindow);
}

bool FEditorEngine::InitializeWorlds(int32 Width, int32 Height)
{
	return InitEditorWorlds(Width, Height);
}

bool FEditorEngine::InitializeMode()
{
	// 에디터 전용 초기화는 규약상 이 단계에서만 수행한다.
	if (!InitEditorPreview())
	{
		return false;
	}

	InitEditorConsole();

	if (!InitEditorCamera())
	{
		return false;
	}

	InitEditorViewportRouting();
	return true;
}

void FEditorEngine::FinalizeInitialize()
{
	// 모드 전용 초기화가 모두 끝난 뒤 마지막 상태를 기록한다.
	UE_LOG("EditorEngine initialized");
	const int32 W = MainWindow ? MainWindow->GetWidth() : 800;
	const int32 H = MainWindow ? MainWindow->GetHeight() : 600;

	TArray<FViewport>& Viewports = ViewportRegistry.GetViewports();
	FViewport* VPs[MAX_VIEWPORTS] = {
		&Viewports[0], &Viewports[1], &Viewports[2], &Viewports[3]
	};
	SlateApplication = std::make_unique<FSlateApplication>();
	SlateApplication->Initialize(FRect(0, 0, W, H), VPs, MAX_VIEWPORTS);
	EditorUI.OnSlateReady();
	CreateInitUI();
	FObjManager::PreloadAllModelFiles(FPaths::FromPath(FPaths::MeshDir()).c_str());
}

void FEditorEngine::PrepareFrame(float DeltaTime)
{
	if (IsPlayingInEditor())
	{
		if (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			EndPIE();
		}

		// F8: Possess/Eject 토글
		static bool bF8WasDown = false;
		bool bF8IsDown = (::GetAsyncKeyState(VK_F8) & 0x8000) != 0;
		if (bF8IsDown && !bF8WasDown)
		{
			if (FInputManager* Input = GetInputManager())
			{
				Input->SetMouseCapture(!Input->IsMouseCaptured());
				UE_LOG("[PIE] %s", Input->IsMouseCaptured() ? "Possessed" : "Ejected");
			}
		}
		bF8WasDown = bF8IsDown;
	}

	SyncViewportClient();
	SyncFocusedViewportLocalState();
	CameraSubsystem.PrepareFrame(GetActiveWorld(), GetLevel(), DeltaTime);

	if (IsPlayingInEditor())
	{
		// ESC로 PIE 종료
		FInputManager* Input = GetInputManager();
		if (Input && Input->IsKeyDown(VK_ESCAPE))
		{
			EndPIE();
			return;
		}

		// PIE 중 우클릭 + WASD/마우스로 카메라 이동
		TickPIECamera(DeltaTime);
	}
}

void FEditorEngine::TickWorlds(float DeltaTime)
{
	if (UWorld* ActiveWorld = GetActiveWorld())
	{
		ActiveWorld->Tick(DeltaTime);
	}
}

std::unique_ptr<IViewportClient> FEditorEngine::CreateViewportClient()
{
	auto Client = std::make_unique<FEditorViewportClient>(*this, EditorUI, ViewportRegistry, MainWindow);
	EditorViewportClientRaw = Client.get();
	return Client;
}

void FEditorEngine::RenderFrame()
{
	FRenderer* Renderer = GetRenderer();
	if (!Renderer || Renderer->IsOccluded())
	{
		return;
	}

	Renderer->BeginFrame();

	// EditorViewportClientRaw를 직접 참조하면 PIE 중에도 에디터 렌더러(기즈모, 선택 등)가 계속 사용됨.
	// GetViewportClient()를 통해 현재 활성 ViewportClient를 가져오면
	// SyncViewportClient에서 교체된 FGameViewportClient가 PIE 중 자동으로 사용됨.
	/*if (EditorViewportClientRaw)
	{
		EditorViewportClientRaw->Render(this, Renderer);
	}*/
	if (IsPlayingInEditor() && PIEViewportClient && EditorViewportClientRaw)
	{
		// ── PIE 렌더 전략 ──────────────────────────────────────────────────
		// 1) 에디터 뷰포트 클라이언트로 에디터 UI(ImGui 메뉴바·아웃라이너 등)를 먼저 렌더한다.
		EditorViewportClientRaw->Render(this, Renderer);

		// UI 렌더 중 Stop 버튼이나 ESC로 EndPIE()가 호출될 수 있으므로
		// 같은 프레임에서 PIE 전용 클라이언트를 다시 확인한다.
		if (!IsPlayingInEditor())
		{
			Renderer->EndFrame();
			return;
		}

		// 2) 포커스된 뷰포트 패널 영역에만 게임 화면을 덮어쓴다.
		//    패널 rect를 D3D11 Viewport로 설정 후 FGameViewportClient를 호출한다.
		FSlateApplication* Slate = SlateApplication.get();
		FViewportId FocusedId = Slate ? Slate->GetFocusedViewportId() : INVALID_VIEWPORT_ID;
		FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(FocusedId);
		if (!Entry && !ViewportRegistry.GetEntries().empty())
		{
			Entry = &ViewportRegistry.GetEntries().front();
		}

		if (Entry && Entry->Viewport)
		{
			const FRect& Rect = Entry->Viewport->GetRect();
			if (Rect.Width > 0 && Rect.Height > 0)
			{
				// PIE 카메라 종횡비를 현재 뷰포트 패널에 맞춘다.
				UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
				if (PIEWorld)
				{
					if (UCameraComponent* Cam = PIEWorld->GetActiveCameraComponent())
					{
						if (Cam->GetCamera())
						{
							Cam->GetCamera()->SetAspectRatio(
								static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height));
						}
					}
				}

				// D3D11 뷰포트를 패널 rect로 제한한 뒤 게임 씬을 렌더한다.
				D3D11_VIEWPORT VP = {};
				VP.TopLeftX = static_cast<float>(Rect.X);
				VP.TopLeftY = static_cast<float>(Rect.Y);
				VP.Width = static_cast<float>(Rect.Width);
				VP.Height = static_cast<float>(Rect.Height);
				VP.MinDepth = 0.0f;
				VP.MaxDepth = 1.0f;
				Renderer->GetDeviceContext()->RSSetViewports(1, &VP);

				PIEViewportClient->Render(this, Renderer);
			}
		}
	}
	if (IViewportClient* ActiveClient = GetViewportClient())
	{
		ActiveClient->Render(this, Renderer);
	}

	Renderer->EndFrame();
}

void FEditorEngine::SyncPlatformState()
{
	SyncPlatformCursor();
}

FEditorViewportController* FEditorEngine::GetViewportController()
{
	return CameraSubsystem.GetViewportController();
}

void FEditorEngine::FlushDebugDrawForViewport(FRenderer* Renderer, const FShowFlags& ShowFlags, bool bClearAfterFlush)
{
	if (!Renderer)
	{
		return;
	}

	if (UWorld* ActiveWorld = GetActiveWorld())
	{
		GetDebugDrawManager().Flush(Renderer, ShowFlags, ActiveWorld, bClearAfterFlush);
	}
	else if (bClearAfterFlush)
	{
		GetDebugDrawManager().Clear();
	}
}

void FEditorEngine::ClearDebugDrawForFrame()
{
	GetDebugDrawManager().Clear();
}

void FEditorEngine::CreateInitUI()
{
	auto* RawEditorVP = static_cast<FEditorViewportClient*>(ViewportClient.get());
	std::unique_ptr<SEditorViewportOverlay> Overlay = std::make_unique<SEditorViewportOverlay>(this, &EditorUI, RawEditorVP);
	SWidget* RawOverlay = SlateApplication->CreateWidget(std::move(Overlay));
	SlateApplication->AddOverlayWidget(RawOverlay);
}

bool FEditorEngine::InitEditorPreview()
{
	// 에디터가 항상 접근 가능한 기본 프리뷰 월드와 프리뷰 뷰포트를 준비한다.
	InitializeDefaultPreviewLevel(this);
	PreviewViewportClient = std::make_unique<FPreviewViewportClient>(EditorUI, PreviewLevelContextName);
	return PreviewViewportClient != nullptr;
}

void FEditorEngine::InitEditorConsole()
{
	FConsoleVariableManager& CVM = FConsoleVariableManager::Get();

	// 현재 등록된 콘솔 변수/명령을 UI 자동완성 목록에 반영한다.
	CVM.GetAllNames([this](const FString& Name)
	{
		EditorUI.GetConsole().RegisterCommand(Name.c_str());
	});

	EditorUI.GetConsole().SetCommandHandler([](const char* CommandLine)
	{
		FString Result;
		if (FConsoleVariableManager::Get().Execute(CommandLine, Result))
		{
			FEngineLog::Get().Log("%s", Result.c_str());
		}
		else
		{
			FEngineLog::Get().Log("[error] Unknown command: '%s'", CommandLine);
		}
	});
}

bool FEditorEngine::InitEditorCamera()
{
	// 에디터 카메라는 월드가 준비된 뒤에만 생성할 수 있다.
	return CameraSubsystem.Initialize(GetActiveWorld(), GetInputManager(), GetEnhancedInputManager());
}

void FEditorEngine::InitEditorViewportRouting()
{
	// 초기 활성 월드가 Editor/Preview 중 무엇인지에 따라 적절한 뷰포트를 고른다.
	SyncViewportClient();

	// Perspective Entry의 LocalState를 입력 컨트롤러에 연결
	FViewportEntry* PerspEntry = nullptr;
	if (SlateApplication)
	{
		const FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
		if (FocusedId != INVALID_VIEWPORT_ID)
		{
			FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
			if (FocusedEntry &&
				FocusedEntry->bActive &&
				FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
			{
				PerspEntry = FocusedEntry;
			}
		}
	}
	if (!PerspEntry)
	{
		PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
	}
	if (PerspEntry)
	{
		CameraSubsystem.GetViewportController()->SetActiveLocalState(&PerspEntry->LocalState);
	}
}

bool FEditorEngine::InitEditorWorlds(int32 Width, int32 Height)
{
	const float AspectRatio = (Height > 0)
		? (static_cast<float>(Width) / static_cast<float>(Height))
		: 1.0f;

	EditorWorldContext = CreateWorldContext("EditorLevel", EWorldType::Editor, AspectRatio, true);
	if (!EditorWorldContext)
	{
		return false;
	}

	// 레벨에 CameraActor가 없으면 기본 카메라 액터를 하나 스폰한다.
	// DefaultLevel.json에 저장된 CameraActor가 있으면 이미 로드됐으므로 중복 생성하지 않는다.
	EnsureDefaultCameraActor();

	ActivateEditorLevel();
	return true;
}

void FEditorEngine::EnsureDefaultCameraActor()
{
	ULevel* Level = GetEditorLevel();
	if (!Level) return;

	for (AActor* Actor : Level->GetActors())
	{
		if (Actor && Actor->IsA(ACameraActor::StaticClass()))
		{
			return; // 이미 존재하면 추가 생성 생략
		}
	}

	ACameraActor* DefaultCam = Level->SpawnActor<ACameraActor>("DefaultCameraActor");
	if (DefaultCam)
	{
		// UE 기본 카메라 위치와 동일하게 설정
		DefaultCam->SetActorLocation(FVector(-5.0f, 0.0f, 2.0f));
		UE_LOG("[Editor] Default CameraActor spawned.");
	}
}

void FEditorEngine::ReleaseEditorWorlds()
{
	ActiveEditorWorldContext = nullptr;

	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		DestroyWorldContext(PreviewContext);
	}
	PreviewWorldContexts.clear();

	DestroyWorldContext(EditorWorldContext);
	EditorWorldContext = nullptr;
}

FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName)
{
	for (FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}

	return nullptr;
}

const FWorldContext* FEditorEngine::FindPreviewWorld(const FString& ContextName) const
{
	for (const FWorldContext* Context : PreviewWorldContexts)
	{
		if (Context && Context->ContextName == ContextName)
		{
			return Context;
		}
	}

	return nullptr;
}

void FEditorEngine::UpdateEditorWorldAspectRatio(float AspectRatio)
{
	UpdateWorldAspectRatio(EditorWorldContext ? EditorWorldContext->World : nullptr, AspectRatio);

	for (FWorldContext* PreviewContext : PreviewWorldContexts)
	{
		UpdateWorldAspectRatio(PreviewContext ? PreviewContext->World : nullptr, AspectRatio);
	}
}

void FEditorEngine::SyncFocusedViewportLocalState()
{
	if (!EditorViewportClientRaw || !SlateApplication)
	{
		return;
	}

	if (IsPlayingInEditor())
	{
		// PIE 중 우클릭 + WASD 입력은 TickPIECamera()가 전담한다.
		// 에디터 viewport LocalState까지 입력 컨트롤러에 연결하면
		// PIE 종료 후 에디터 시점이 같이 이동한 것처럼 보이게 된다.
		CameraSubsystem.GetViewportController()->SetActiveLocalState(nullptr);
		return;
	}

	FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
	FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
	FViewportLocalState* LocalState = nullptr;
	if (FocusedEntry && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
	{
		LocalState = &FocusedEntry->LocalState;
	}

	CameraSubsystem.GetViewportController()->SetActiveLocalState(LocalState);
}

void FEditorEngine::SyncPlatformCursor()
{
	if (!SlateApplication || !SlateApplication->GetIsCoursorInArea())
	{
		return;
	}

	const EMouseCursor SlateCursor = SlateApplication->GetCurrentCursor();
	LPCWSTR WinCursorName = IDC_ARROW;
	switch (SlateCursor)
	{
	case EMouseCursor::Default:         WinCursorName = IDC_ARROW;  break;
	case EMouseCursor::ResizeLeftRight: WinCursorName = IDC_SIZEWE; break;
	case EMouseCursor::ResizeUpDown:    WinCursorName = IDC_SIZENS; break;
	case EMouseCursor::Hand:            WinCursorName = IDC_HAND;   break;
	case EMouseCursor::None:            WinCursorName = nullptr;    break;
	}

	if (WinCursorName)
	{
		::SetCursor(::LoadCursor(NULL, WinCursorName));
	}
	else
	{
		::SetCursor(nullptr);
	}
}

void FEditorEngine::SyncViewportClient()
{
	if (!GetActiveWorldContext())
	{
		return;
	}

	IViewportClient* TargetViewportClient = ViewportClient.get();
	const FWorldContext* ActiveLevelContext = GetActiveWorldContext();
	if (ActiveLevelContext && ActiveLevelContext->WorldType == EWorldType::Preview && PreviewViewportClient)
	{
		TargetViewportClient = PreviewViewportClient.get();
	}
	else if (ActiveLevelContext && ActiveLevelContext->WorldType == EWorldType::PIE)
	{
		// PIE 진입 시 FGameViewportClient로 전환해 에디터 기즈모, 선택 영역 없이 게임 렌더를 수행함.
		// FGameViewportClient가 없으면 지연 생성함.
		if (!PIEViewportClient)
		{
			PIEViewportClient = std::make_unique<FGameViewportClient>();
		}
		TargetViewportClient = PIEViewportClient.get();
	}

	if (GetViewportClient() != TargetViewportClient)
	{
		SetViewportClient(TargetViewportClient);
	}
}

FViewport* FEditorEngine::FindViewport(FViewportId Id)
{
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		if (Entry.Id == Id && Entry.bActive)
		{
			return Entry.Viewport;
		}
	}

	return nullptr;
}

void FEditorEngine::TickPIECamera(float DeltaTime)
{
	// PIE 중 우클릭 + WASD/마우스로 에디터 플라이스루와 동일한 방식으로 카메라를 조작한다.
	// 에디터 카메라(FEditorCameraSubsystem)와 독립적으로 PIEWorld의 ActiveCamera를 직접 구동한다.
	FInputManager* Input = GetInputManager();
	if (!Input) return;

	// 우클릭이 없으면 카메라 입력을 무시한다.
	if (!Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT)) return;

	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
	if (!PIEWorld) return;

	UCameraComponent* CamComp = PIEWorld->GetActiveCameraComponent();
	if (!CamComp) return;

	FCamera* Camera = CamComp->GetCamera();
	if (!Camera) return;

	// 마우스 델타로 Yaw/Pitch 회전
	const float Sensitivity = Camera->GetMouseSensitivity();
	const float DX = Input->GetMouseDeltaX();
	const float DY = Input->GetMouseDeltaY();
	Camera->Rotate(DX * Sensitivity, -DY * Sensitivity);

	// WASD + Q/E 로 이동 (FCamera::MoveXxx 는 내부에서 Speed * Delta 적용)
	if (Input->IsKeyDown('W')) Camera->MoveForward(DeltaTime);
	if (Input->IsKeyDown('S')) Camera->MoveForward(-DeltaTime);
	if (Input->IsKeyDown('A')) Camera->MoveRight(-DeltaTime);
	if (Input->IsKeyDown('D')) Camera->MoveRight(DeltaTime);
	if (Input->IsKeyDown('E')) Camera->MoveUp(DeltaTime);
	if (Input->IsKeyDown('Q')) Camera->MoveUp(-DeltaTime);
}