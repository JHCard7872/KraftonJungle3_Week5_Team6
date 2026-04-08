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
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Object/ObjectFactory.h"
#include "Platform/Windows/WindowsWindow.h"
#include "Scene/Level.h"
#include "Viewport/Viewport.h"
#include "Viewport/EditorViewportClient.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Material.h"
#include "Viewport/PreviewViewportClient.h"
#include "World/World.h"
#include "Slate/EditorViewportOverlay.h"
#include "Pawn/EditorCameraPawn.h"

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
	if (IsPlayingInEditor() || !EditorWorldContext || !EditorWorldContext->World)
	{
		return;
	}

	UE_LOG("[PIE] Play In Editor Started.");

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

	// 3. 에디터 폰 좌표 즉시 동기화
	if (AEditorCameraPawn* EditorPawn = CameraSubsystem.GetEditorCameraPawn())
	{
		EditorPawn->SetActorLocation(EditorCamPos);
		if (UCameraComponent* EditorCamComp = EditorPawn->GetCameraComponent())
		{
			EditorCamComp->GetCamera()->SetRotation(EditorCamYaw, EditorCamPitch);
		}
	}

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
	PIEWorldContext->World = PIEWorld;
	PIEWorld->SetWorldType(EWorldType::PIE);
	
	ActiveEditorWorldContext = PIEWorldContext;

	// 4. 카메라 설정 우선순위
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
					CameraComponent->ApplyComponentTransformToCamera();
					PIEWorld->SetActiveCameraComponent(CameraComponent);
					UE_LOG("[PIE] Camera possession: ACameraActor");
					bPossessedCameraActor = true;
				}
				break;
			}
		}
	}

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

	// 시작 즉시 PIE 뷰포트 마우스 캡처 및 중앙 고정
	if (FInputManager* Input = GetInputManager())
	{
		Input->SetMouseCapture(true);
		if (MainWindow)
		{
			const FViewportId PIEId = GetPIEViewportId();
			const FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(PIEId);
			if (Entry && Entry->Viewport)
			{
				const FRect& Rect = Entry->Viewport->GetRect();
				POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
				::ClientToScreen(MainWindow->GetHwnd(), &Center);
				::SetCursorPos(Center.x, Center.y);
			}
		}
	}
}

void FEditorEngine::EndPIE()
{
	if (!IsPlayingInEditor())
	{
		return;
	}

	UE_LOG("[PIE] Play In Editor Stopped.");

	GetActiveWorld()->SetPaused(false);

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

	if (IViewportClient* ActiveClient = GetViewportClient())
	{
		if (ActiveClient == PreviewViewportClient.get() || ActiveClient == PIEViewportClient.get())
		{
			SetViewportClient(nullptr);
		}
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

FViewportId FEditorEngine::GetPIEViewportId() const
{
	if (!IsPlayingInEditor())
	{
		return INVALID_VIEWPORT_ID;
	}

	FSlateApplication* Slate = GetSlateApplication();
	FViewportId FocusedId = Slate ? Slate->GetFocusedViewportId() : INVALID_VIEWPORT_ID;

	// 1순위: 현재 포커스된 창이 Perspective라면 그걸로 당첨!
	const FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
	if (FocusedEntry && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
	{
		return FocusedId;
	}

	// 2순위: 포커스된 창이 Ortho거나 다른 UI라면, 무조건 0순위 Perspective 창을 강제 할당!
	const FViewportEntry* PerspEntry = ViewportRegistry.FindEntryByType(EViewportType::Perspective);
	if (PerspEntry)
	{
		return PerspEntry->Id;
	}

	return INVALID_VIEWPORT_ID;
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
	FViewport* VPs[MAX_VIEWPORTS] = { &Viewports[0], &Viewports[1], &Viewports[2], &Viewports[3] };
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
		FInputManager* Input = GetInputManager();
		if (Input && Input->IsKeyDown(VK_ESCAPE))
		{
			EndPIE();
			return;
		}

		// F8: Possess/Eject 토글
		static bool bF8WasDown = false;
		bool bF8IsDown = (::GetAsyncKeyState(VK_F8) & 0x8000) != 0;

		if (bF8IsDown && !bF8WasDown)
		{
			if (Input)
			{
				bool bNewCapture = !Input->IsMouseCaptured();
				Input->SetMouseCapture(bNewCapture);

				if (bNewCapture)
				{
					bPIEJustCaptured = true; // 캡처됨 마킹
					if (MainWindow)
					{
						const FViewportId PIEId = GetPIEViewportId();
						const FViewportEntry* Entry = ViewportRegistry.FindEntryByViewportID(PIEId);
						if (Entry && Entry->Viewport)
						{
							const FRect& Rect = Entry->Viewport->GetRect();
							POINT Center = { Rect.X + Rect.Width / 2, Rect.Y + Rect.Height / 2 };
							::ClientToScreen(MainWindow->GetHwnd(), &Center);
							::SetCursorPos(Center.x, Center.y);
						}
					}
				}
				UE_LOG("[PIE] %s", Input->IsMouseCaptured() ? "Possessed (FPS Mode)" : "Ejected (Editor Mode)");
			}
		}
		bF8WasDown = bF8IsDown;
	}

	// [Possessed 상태 시점 보존 핵심]: 뷰포트 포커스 여부와 상관없이 모든 퍼스펙티브 뷰포트에 PIE 카메라를 강제 주입
	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		UCameraComponent* PIECamComp = PIEWorld ? PIEWorld->GetActiveCameraComponent() : nullptr;

		if (Input && Input->IsMouseCaptured() && PIECamComp && PIECamComp->GetCamera())
		{
			FCamera* Cam = PIECamComp->GetCamera();

			// 모든 퍼스펙티브 엔트리를 업데이트함 (첫 F8 점프 방지)
			for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
			{
				if (Entry.LocalState.ProjectionType == EViewportType::Perspective)
				{
					Entry.LocalState.Position = Cam->GetPosition();
					Entry.LocalState.Rotation.Yaw = Cam->GetYaw();
					Entry.LocalState.Rotation.Pitch = Cam->GetPitch();
					Entry.LocalState.FovY = Cam->GetFOV();
				}
			}

			if (FEditorViewportController* Controller = CameraSubsystem.GetViewportController())
			{
				if (FViewportLocalState* ActiveState = Controller->GetActiveLocalState())
				{
					ActiveState->Position = Cam->GetPosition();
					ActiveState->Rotation.Yaw = Cam->GetYaw();
					ActiveState->Rotation.Pitch = Cam->GetPitch();
					ActiveState->FovY = Cam->GetFOV();
				}
			}

			if (AEditorCameraPawn* EditorPawn = CameraSubsystem.GetEditorCameraPawn())
			{
				EditorPawn->SetActorLocation(Cam->GetPosition());
				if (UCameraComponent* EditorCamComp = EditorPawn->GetCameraComponent())
				{
					EditorCamComp->GetCamera()->SetRotation(Cam->GetYaw(), Cam->GetPitch());
					EditorCamComp->SetFov(Cam->GetFOV());
				}
			}
		}
	}

	SyncViewportClient();
	SyncFocusedViewportLocalState();
	CameraSubsystem.PrepareFrame(GetActiveWorld(), GetLevel(), DeltaTime);

	if (IsPlayingInEditor())
	{
		FInputManager* Input = GetInputManager();
		UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
		UCameraComponent* PIECamComp = PIEWorld ? PIEWorld->GetActiveCameraComponent() : nullptr;

		if (Input && PIECamComp && PIECamComp->GetCamera())
		{
			if (!Input->IsMouseCaptured())
			{
				// [Ejected 상태]: 에디터 조작계의 값들을 PIE 카메라에 투영
				FViewportId FocusedId =
					SlateApplication ? SlateApplication->GetFocusedViewportId() : INVALID_VIEWPORT_ID;
				FViewportEntry* FocusedEntry = ViewportRegistry.FindEntryByViewportID(FocusedId);
				if (!FocusedEntry && !ViewportRegistry.GetEntries().empty())
				{
					FocusedEntry = &ViewportRegistry.GetEntries().front();
				}

				if (FocusedEntry)
				{
					FCamera* Cam = PIECamComp->GetCamera();
					Cam->SetPosition(FocusedEntry->LocalState.Position);
					Cam->SetRotation(FocusedEntry->LocalState.Rotation.Yaw, FocusedEntry->LocalState.Rotation.Pitch);
					PIECamComp->SetFov(FocusedEntry->LocalState.FovY);
				}
			}
			else
			{
				// 마우스가 캡처된 상태일 때만 PIE 카메라 조작 (자동 회전 포함)
				if (bPIEJustCaptured)
				{
					bPIEJustCaptured = false;
				}
				else
				{
					TickPIECamera(DeltaTime);
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
	if (!Renderer || Renderer->IsOccluded()) return;

	Renderer->BeginFrame();
	IViewportClient* ActiveClient = GetViewportClient();

	if (IsPlayingInEditor() && PIEViewportClient && EditorViewportClientRaw)
	{
		if (ActiveClient != EditorViewportClientRaw)
		{
			EditorViewportClientRaw->Render(this, Renderer);
		}
		if (!IsPlayingInEditor())
		{
			Renderer->EndFrame();
			return;
		}
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
				UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
				if (Rect.Width > 0 && Rect.Height > 0)
				{
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
					D3D11_VIEWPORT VP = { static_cast<float>(Rect.X), static_cast<float>(Rect.Y),
						static_cast<float>(Rect.Width), static_cast<float>(Rect.Height), 0.0f, 1.0f };
					Renderer->GetDeviceContext()->RSSetViewports(1, &VP);
					PIEViewportClient->Render(this, Renderer);
				}

				// D3D11 뷰포트를 패널 rect로 제한한 뒤 게임 씬을 렌더한다.
				Renderer->SetRenderViewport(static_cast<float>(Rect.X), static_cast<float>(Rect.Y),
					static_cast<float>(Rect.Width), static_cast<float>(Rect.Height), 0.0f, 1.0f);

				PIEViewportClient->Render(this, Renderer);

				// ── PIE 그리드 렌더링 ────────────────────────────────────
				// EditorViewportClient가 생성·보유한 GridMesh/GridMaterial을 재사용해
				// PIE 화면에도 XY 평면 그리드를 그린다.
				if (Entry->LocalState.bShowGrid)
				{
					FDynamicMesh* GridMesh = EditorViewportClientRaw->GetGridMesh();
					FMaterial* GridMat = EditorViewportClientRaw->GetGridMaterial();

					if (GridMesh && GridMat && PIEWorld)
					{
						if (UCameraComponent* PIECam = PIEWorld->GetActiveCameraComponent())
						{
							FRenderCommandQueue GridQueue;
							GridQueue.ViewMatrix = PIECam->GetViewMatrix();
							GridQueue.ProjectionMatrix = PIECam->GetProjectionMatrix();

							// Perspective: XZ 평면(ForwardVector/RightVector) 고정
							const FVector GridAxisU = FVector::ForwardVector;
							const FVector GridAxisV = FVector::RightVector;
							const FVector ViewForward =
								GridQueue.ViewMatrix.GetInverse().GetForwardVector().GetSafeNormal();

							GridMat->SetParameterData("GridSize", &Entry->LocalState.GridSize, 4);
							GridMat->SetParameterData("LineThickness", &Entry->LocalState.LineThickness, 4);
							GridMat->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector));
							GridMat->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector));
							GridMat->SetParameterData("ViewForward", &ViewForward, sizeof(FVector));

							FRenderCommand GridCommand;
							GridCommand.RenderMesh = GridMesh;
							GridCommand.Material = GridMat;
							GridCommand.WorldMatrix = FMatrix::Identity;
							GridCommand.RenderLayer = ERenderLayer::Default;
							GridQueue.AddCommand(GridCommand);

							Renderer->SubmitCommands(GridQueue);
							Renderer->ExecuteCommands();
						}
					}
				}
			}
			Renderer->EndFrame();
			return;
		}
	}
	if (ActiveClient)
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
	std::unique_ptr<SEditorViewportOverlay> Overlay =
		std::make_unique<SEditorViewportOverlay>(this, &EditorUI, RawEditorVP);
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
			if (FocusedEntry && FocusedEntry->bActive &&
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
	const float AspectRatio = (Height > 0) ? (static_cast<float>(Width) / static_cast<float>(Height)) : 1.0f;
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
	if (!Level)
	{
		return;
	}
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
	FViewportLocalState* LocalState = (FocusedEntry && FocusedEntry->LocalState.ProjectionType == EViewportType::Perspective)
		? &FocusedEntry->LocalState
		: nullptr;
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
	case EMouseCursor::Default: WinCursorName = IDC_ARROW; break;
	case EMouseCursor::ResizeLeftRight: WinCursorName = IDC_SIZEWE; break;
	case EMouseCursor::ResizeUpDown: WinCursorName = IDC_SIZENS; break;
	case EMouseCursor::Hand: WinCursorName = IDC_HAND; break;
	case EMouseCursor::None: WinCursorName = nullptr; break;
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
		FInputManager* Input = GetInputManager();
		if (Input && !Input->IsMouseCaptured())
		{
			TargetViewportClient = EditorViewportClientRaw;
		}
		else
		{
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
	if (!Input)
	{
		return;
	}
	
	UWorld* PIEWorld = PIEWorldContext ? PIEWorldContext->World : nullptr;
	if (!PIEWorld)
	{
		return;
	}
	UCameraComponent* CamComp = PIEWorld->GetActiveCameraComponent();
	if (!CamComp)
	{
		return;
	}
	FCamera* Camera = CamComp->GetCamera();
	if (!Camera)
	{
		return;
	}

	// 마우스가 캡처된 상태라면 우클릭 없이도 회전 가능
	if (Input->IsMouseCaptured())
	{
		const float Sensitivity = Camera->GetMouseSensitivity();
		const float DX = Input->GetMouseDeltaX();
		const float DY = Input->GetMouseDeltaY();
		Camera->Rotate(DX * Sensitivity, -DY * Sensitivity);
	}

	if (Input->IsKeyDown('W'))
	{
		Camera->MoveForward(DeltaTime);
	}
	if (Input->IsKeyDown('S'))
	{
		Camera->MoveForward(-DeltaTime);
	}
	if (Input->IsKeyDown('A'))
	{
		Camera->MoveRight(-DeltaTime);
	}
	if (Input->IsKeyDown('D'))
	{
		Camera->MoveRight(DeltaTime);
	}
	if (Input->IsKeyDown('E'))
	{
		Camera->MoveUp(DeltaTime);
	}
	if (Input->IsKeyDown('Q'))
	{
		Camera->MoveUp(-DeltaTime);
	}
}
