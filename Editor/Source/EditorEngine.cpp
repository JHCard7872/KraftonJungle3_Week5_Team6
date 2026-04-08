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

	// 1. 에디터 카메라 상태 백업
	EditorCameraStatesBackup.clear();
	for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
	{
		EditorCameraStatesBackup[Entry.Id] = Entry.LocalState;
	}

	// 2. 현재 에디터 카메라 정보 저장
	FVector EditorCamPos;
	float EditorCamYaw = 0.0f;
	float EditorCamPitch = 0.0f;
	if (UCameraComponent* EditorCam = EditorWorldContext->World->GetActiveCameraComponent())
	{
		EditorCamPos = EditorCam->GetCamera()->GetPosition();
		EditorCamYaw = EditorCam->GetCamera()->GetYaw();
		EditorCamPitch = EditorCam->GetCamera()->GetPitch();
	}

	// PIE 진입 전 에디터 선택 해제
	SetSelectedActor(nullptr);

	FDuplicateionContext Context;
	UWorld* EditorWorld = EditorWorldContext->World;

	UWorld* PIEWorld = static_cast<UWorld*>(EditorWorld->Duplicate(Context, nullptr));
	for (auto& [OldObj, NewObj] : Context.DuplicatedObjects)
	{
		NewObj->FixupReferences(Context);
	}

	float AspectRatio = MainWindow ? (static_cast<float>(MainWindow->GetWidth()) / static_cast<float>(MainWindow->GetHeight())) : 1.0f;
	PIEWorldContext = CreateWorldContext("PIELevel", EWorldType::PIE, AspectRatio, false);

	if (PIEWorldContext->World && PIEWorldContext->World != PIEWorld)
	{
		PIEWorldContext->World = PIEWorld;
	}

	PIEWorld->SetWorldType(EWorldType::PIE);
	ActiveEditorWorldContext = PIEWorldContext;

	// 3. 카메라 설정 우선순위
	bool bPossessedCameraActor = false;
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
					bPossessedCameraActor = true;
				}
				break;
			}
		}
	}

	// 배치된 카메라 액터가 없는 경우에만 에디터 카메라 위치를 복제본에 적용
	if (!bPossessedCameraActor)
	{
		if (UCameraComponent* PIECam = PIEWorld->GetActiveCameraComponent())
		{
			PIECam->GetCamera()->SetPosition(EditorCamPos);
			PIECam->GetCamera()->SetRotation(EditorCamYaw, EditorCamPitch);
		}
	}

	PIEWorld->BeginPlay();
	SyncViewportClient();

	// 시작 시 즉시 마우스 캡처(Possessed) 상태로 진입
	if (FInputManager* Input = GetInputManager())
	{
		Input->SetMouseCapture(true);
	}
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

	ActiveEditorWorldContext = EditorWorldContext;

	if (PIEWorldContext)
	{
		DestroyWorldContext(PIEWorldContext);
		PIEWorldContext = nullptr;
	}
	
	SetSelectedActor(nullptr);
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
	ImGui_ImplWin32_EnableDpiAwareness();

	FEngineLog::Get().SetCallback([this](const char* Msg)
	{
		EditorUI.GetConsole().AddLog("%s", Msg);
	});
}

void FEditorEngine::BindHost(FWindowsWindow* InMainWindow)
{
	MainWindow = InMainWindow;
	EditorUI.SetupWindow(InMainWindow);
}

bool FEditorEngine::InitializeWorlds(int32 Width, int32 Height)
{
	return InitEditorWorlds(Width, Height);
}

bool FEditorEngine::InitializeMode()
{
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
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		UCameraComponent* PIECamComp = PIEWorld ? PIEWorld->GetActiveCameraComponent() : nullptr;

		if (Input && PIECamComp)
		{
			if (!Input->IsMouseCaptured())
			{
				// [Ejected 상태]: 에디터 조작계(LocalState)의 값들을 PIE 카메라에 매 프레임 강제 투영
				if (SlateApplication)
				{
					FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
					FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
					if (FocusedEntry)
					{
						if (FCamera* Cam = PIECamComp->GetCamera())
						{
							Cam->SetPosition(FocusedEntry->LocalState.Position);
							Cam->SetRotation(FocusedEntry->LocalState.Rotation.Yaw, FocusedEntry->LocalState.Rotation.Pitch);
							PIECamComp->SetFov(FocusedEntry->LocalState.FovY);
						}
					}
				}
			}
			else
			{
				// [Possessed 상태]: PIE 월드 전용 FPS 조작(WASD+마우스) 수행
				TickPIECamera(DeltaTime);

				// 시점 역동기화: 현재 조작 중인 PIE 카메라 데이터를 LocalState에 실시간 저장
				// 이 로직 덕분에 F8을 누르는 순간 시점 점프 없이 그 자리에서 마우스만 나타남
				if (SlateApplication)
				{
					FViewportId FocusedId = SlateApplication->GetFocusedViewportId();
					FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
					if (FocusedEntry)
					{
						if (FCamera* Cam = PIECamComp->GetCamera())
						{
							FocusedEntry->LocalState.Position = Cam->GetPosition();
							FocusedEntry->LocalState.Rotation.Yaw = Cam->GetYaw();
							FocusedEntry->LocalState.Rotation.Pitch = Cam->GetPitch();
							FocusedEntry->LocalState.FovY = Cam->GetFOV();
						}
					}
				}
			}
		}
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

	IViewportClient* ActiveClient = GetViewportClient();

	if (IsPlayingInEditor() && PIEViewportClient && EditorViewportClientRaw)
	{
		// 1) 에디터 UI 및 기본 레이아웃 렌더링 (Ejected 상태라면 ActiveClient가 이미 Editor이므로 중복 호출 방지)
		if (ActiveClient != EditorViewportClientRaw)
		{
			EditorViewportClientRaw->Render(this, Renderer);
		}

		if (!IsPlayingInEditor())
		{
			Renderer->EndFrame();
			return;
		}

		// 2) 게임 씬 렌더링 (Possessed 상태라면 ActiveClient가 PIE이므로 여기서 직접 패널 영역에 그림)
		if (ActiveClient == PIEViewportClient.get())
		{
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
			
			// 렌더링 완료 후 리턴 (ActiveClient->Render() 중복 호출 방지)
			Renderer->EndFrame();
			return;
		}
	}
	
	if (ActiveClient)
	{
		// 일반 에디터 모드이거나, PIE Ejected 상태일 때는 여기서 최종 렌더링 수행
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
	InitializeDefaultPreviewLevel(this);
	PreviewViewportClient = std::make_unique<FPreviewViewportClient>(EditorUI, PreviewLevelContextName);
	return PreviewViewportClient != nullptr;
}

void FEditorEngine::InitEditorConsole()
{
	FConsoleVariableManager& CVM = FConsoleVariableManager::Get();

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
	return CameraSubsystem.Initialize(GetActiveWorld(), GetInputManager(), GetEnhancedInputManager());
}

void FEditorEngine::InitEditorViewportRouting()
{
	SyncViewportClient();

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
			return; 
		}
	}

	ACameraActor* DefaultCam = Level->SpawnActor<ACameraActor>("DefaultCameraActor");
	if (DefaultCam)
	{
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
		FInputManager* Input = GetInputManager();
		if (Input && Input->IsMouseCaptured())
		{
			CameraSubsystem.GetViewportController()->SetActiveLocalState(nullptr);
			return;
		}
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
		// PIE 모드: 캡처 상태에 따라 조작 주체를 바꿉니다.
		FInputManager* Input = GetInputManager();
		if (Input && !Input->IsMouseCaptured())
		{
			// Ejected: 에디터 클라이언트를 사용하여 버튼 클릭, 기즈모 조작 가능
			TargetViewportClient = EditorViewportClientRaw;
		}
		else
		{
			// Possessed: 게임 클라이언트를 사용하여 순수 게임 화면 처리
			if (!PIEViewportClient)
			{
				PIEViewportClient = std::make_unique<FGameViewportClient>();
			}
			TargetViewportClient = PIEViewportClient.get();
		}
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
	FInputManager* Input = GetInputManager();
	if (!Input) return;

	bool bIsCaptured = Input->IsMouseCaptured();
	bool bRightMouseDown = Input->IsMouseButtonDown(FInputManager::MOUSE_RIGHT);
	bool bInArea = SlateApplication ? SlateApplication->GetIsCoursorInArea() : false;

	// [Ejected 상태 조건]: 반드시 뷰포트 영역 내부에 마우스가 있고 우클릭 중이어야 함
	if (!bIsCaptured)
	{
		if (!bInArea || !bRightMouseDown) return;
		
		// ImGui 패널 조작 중이면 차단 (우클릭 중이 아닐 때만)
		if (ImGui::GetCurrentContext() && !bRightMouseDown && ImGui::GetIO().WantCaptureMouse) return;
	}

	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
	if (!PIEWorld) return;

	UCameraComponent* CamComp = PIEWorld->GetActiveCameraComponent();
	if (!CamComp) return;

	FCamera* Camera = CamComp->GetCamera();
	if (!Camera) return;

	const float Sensitivity = Camera->GetMouseSensitivity();
	const float DX = Input->GetMouseDeltaX();
	const float DY = Input->GetMouseDeltaY();
	Camera->Rotate(DX * Sensitivity, -DY * Sensitivity);

	if (Input->IsKeyDown('W')) Camera->MoveForward(DeltaTime);
	if (Input->IsKeyDown('S')) Camera->MoveForward(-DeltaTime);
	if (Input->IsKeyDown('A')) Camera->MoveRight(-DeltaTime);
	if (Input->IsKeyDown('D')) Camera->MoveRight(DeltaTime);
	if (Input->IsKeyDown('E')) Camera->MoveUp(DeltaTime);
	if (Input->IsKeyDown('Q')) Camera->MoveUp(-DeltaTime);
}
