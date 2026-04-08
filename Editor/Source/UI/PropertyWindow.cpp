#include "PropertyWindow.h"
#include "EditorEngine.h"
#include "Actor/Actor.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Object/ObjectIterator.h"
#include "Renderer/MeshData.h"
#include "Renderer/RenderMesh.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Component/BillboardComponent.h"
#include "Component/ActorComponent.h"

void FPropertyWindow::SetTarget(const FVector& Location, const FVector& Rotation,
                                const FVector& Scale, const char* ActorName)
{
	EditLocation = Location;
	EditRotation = Rotation;
	EditScale = Scale;
	bModified = false;

	if (ActorName)
		snprintf(ActorNameBuf, sizeof(ActorNameBuf), "%s", ActorName);
	else
		snprintf(ActorNameBuf, sizeof(ActorNameBuf), "None");
}

void FPropertyWindow::DrawTransformSection()
{
	float Loc[3] = { EditLocation.X, EditLocation.Y, EditLocation.Z };
	float Rot[3] = { EditRotation.X, EditRotation.Y, EditRotation.Z };
	float Scl[3] = { EditScale.X,    EditScale.Y,    EditScale.Z };

	const float ResetBtnWidth = 14.0f;
	const float Spacing = ImGui::GetStyle().ItemInnerSpacing.x;
	const float DragUIWidth = 200.f;

	// Location
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
	if (ImGui::Button("##RL", ImVec2(ResetBtnWidth, 0)))
	{
		EditLocation = { 0.0f, 0.0f, 0.0f };
		bModified = true;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Location");
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0, Spacing);
	// ImGui::PushItemWidth(-(ResetBtnWidth));
	ImGui::PushItemWidth(DragUIWidth);
	if (ImGui::DragFloat3("Location", Loc, 0.1f, 0.0f, 0.0f, "%.2f"))
	{
		EditLocation = { Loc[0], Loc[1], Loc[2] };
		bModified = true;
	}
	ImGui::PopItemWidth();

	// Rotation
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
	if (ImGui::Button("##RR", ImVec2(ResetBtnWidth, 0)))
	{
		EditRotation = { 0.0f, 0.0f, 0.0f };
		bModified = true;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Rotation");
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0, Spacing);
	// ImGui::PushItemWidth(-(ResetBtnWidth));
	ImGui::PushItemWidth(DragUIWidth);
	if (ImGui::DragFloat3("Rotation", Rot, 0.5f, -360.0f, 360.0f, "%.1f"))
	{
		EditRotation = { Rot[0], Rot[1], Rot[2] };
		bModified = true;
	}
	ImGui::PopItemWidth();

	// Scale
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.2f, 0.5f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.7f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.4f, 0.9f, 1.0f));
	if (ImGui::Button("##RS", ImVec2(ResetBtnWidth, 0)))
	{
		EditScale = { 1.0f, 1.0f, 1.0f };
		bModified = true;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Scale");
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0, Spacing);
	// ImGui::PushItemWidth(-(ResetBtnWidth));
	ImGui::PushItemWidth(DragUIWidth);
	if (ImGui::DragFloat3("Scale", Scl, 0.01f, 0.001f, 100.0f, "%.3f"))
	{
		EditScale = { Scl[0], Scl[1], Scl[2] };
		bModified = true;
	}
	ImGui::PopItemWidth();

	if (bModified && OnChanged)
		OnChanged(EditLocation, EditRotation, EditScale);
}

void FPropertyWindow::Render(FEditorEngine* Engine)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	bool bOpen = ImGui::Begin("Properties");
	ImGui::PopStyleVar();

	if (!bOpen)
	{
		ImGui::End();
		return;
	}

	bModified = false;

	ImGui::TextDisabled("Selected:");
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.4f, 1.0f), "%s", ActorNameBuf);

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);
		DrawTransformSection();
		ImGui::Unindent(8.0f);
	}
	if (Engine)
	{
		AActor* SelectedActor = Engine->GetSelectedActor();
		if (SelectedActor)
		{
			if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent(8.0f);

				// ─── 컴포넌트 목록 ───
				UActorComponent* PendingRemove = nullptr;

				for (UActorComponent* Component : SelectedActor->GetComponents())
				{
					if (!Component) continue;

					const char* TypeName = nullptr;
					if (Component->IsA(USubUVComponent::StaticClass()))
						TypeName = "SubUVComponent";
					else if (Component->IsA(UTextRenderComponent::StaticClass())
						&& !Component->IsA(UUUIDBillboardComponent::StaticClass()))
						TypeName = "TextRenderComponent";
					else if (Component->IsA(UStaticMeshComponent::StaticClass()))
						TypeName = "StaticMeshComponent";
					else if (Component->IsA(UBillboardComponent::StaticClass()))
						TypeName = "BillboardComponent";
					else
						continue;

					ImGui::PushID(Component);

					bool bIsSelected = (SelectedComponent == Component);
					ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf
						| ImGuiTreeNodeFlags_NoTreePushOnOpen
						| ImGuiTreeNodeFlags_SpanAvailWidth;
					if (bIsSelected)
						Flags |= ImGuiTreeNodeFlags_Selected;

					ImGui::TreeNodeEx(TypeName, Flags);

					if (ImGui::IsItemClicked())
						SelectedComponent = Component;

					if (ImGui::BeginPopupContextItem("##CompContext"))
					{
						if (ImGui::MenuItem("Delete"))
							PendingRemove = Component;
						ImGui::EndPopup();
					}

					ImGui::PopID();
				}

				// 루프 밖에서 삭제
				if (PendingRemove)
				{
					if (SelectedComponent == PendingRemove)
						SelectedComponent = nullptr;
					SelectedActor->RemoveOwnedComponent(PendingRemove);
					PendingRemove->MarkPendingKill();
				}

				ImGui::Separator();

				// ─── 컴포넌트 추가 ───
				const char* AddableComponents[] = {
					"StaticMeshComponent",
					"BillboardComponent",
					"SubUVComponent",
					"TextRenderComponent"
				};
				static int SelectedAddIdx = 0;
				ImGui::PushItemWidth(160.f);
				ImGui::Combo("##AddComp", &SelectedAddIdx, AddableComponents, IM_ARRAYSIZE(AddableComponents));
				ImGui::PopItemWidth();
				ImGui::SameLine();

				if (ImGui::Button("Add Component"))
				{
					// ConstructObject은 이름이 겹치면 안되므로, 간단히 카운터를 붙여서 이름을 생성합니다.
					static uint32 AddCount = 0;
					++AddCount;

					const char* BaseName = AddableComponents[SelectedAddIdx];
					std::string UniqueName = std::string(BaseName) + "_" + std::to_string(AddCount);

					USceneComponent* NewComp = nullptr;
					switch (SelectedAddIdx)
					{
					case 0: NewComp = FObjectFactory::ConstructObject<UStaticMeshComponent>(SelectedActor, UniqueName); break;
					case 1: NewComp = FObjectFactory::ConstructObject<UBillboardComponent>(SelectedActor, UniqueName);  break;
					case 2: NewComp = FObjectFactory::ConstructObject<USubUVComponent>(SelectedActor, UniqueName);      break;
					case 3: NewComp = FObjectFactory::ConstructObject<UTextRenderComponent>(SelectedActor, UniqueName); break;
					}
					if (NewComp)
					{
						SelectedActor->AddOwnedComponent(NewComp);
						NewComp->AttachTo(SelectedActor->GetRootComponent());
						NewComp->OnRegister();
					}
				}

				ImGui::Unindent(8.0f);
			}

			if (SelectedComponent)
			{
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Component Details");
				ImGui::Separator();

				// ─── UBillboardComponent ───
				if (SelectedComponent->IsA(UBillboardComponent::StaticClass())
					&& !SelectedComponent->IsA(USubUVComponent::StaticClass()))
				{
					UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(SelectedComponent);

					if (ImGui::CollapsingHeader("Billboard Sprite", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Indent(8.0f);

						bool bBillboard = BillboardComp->IsBillboard();
						if (ImGui::Checkbox("Billboard", &bBillboard))
							BillboardComp->SetBillboard(bBillboard);

						TArray<FString> MatNames = FMaterialManager::Get().GetAllMaterialNames();
						FMaterial* Mat = BillboardComp->GetBaseMaterial()->GetRenderMaterial();
						std::string CurrentMatName = Mat->GetMaterialTexture() ? Mat->GetOriginName() : "None";

						ImGui::PushItemWidth(180.f);
						if (ImGui::BeginCombo("Sprite", CurrentMatName.c_str()))
						{
							for (const FString& MatName : MatNames)
							{
								ImGui::PushID(MatName.c_str());
								auto ListMaterial = FMaterialManager::Get().FindByName(MatName);
								ImTextureID TexID = (ImTextureID)0;
								if (ListMaterial && ListMaterial->GetMaterialTexture()
									&& ListMaterial->GetMaterialTexture()->TextureSRV)
									TexID = (ImTextureID)ListMaterial->GetMaterialTexture()->TextureSRV;

								if (!TexID) { ImGui::PopID(); continue; }

								ImGui::Image(TexID, ImVec2(24.0f, 24.0f));
								ImGui::SameLine();
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

								bool bSelected = (CurrentMatName == MatName);
								if (ImGui::Selectable(MatName.c_str(), bSelected))
									BillboardComp->SetSpriteTexture(ListMaterial->GetMaterialTexture());
								if (bSelected) ImGui::SetItemDefaultFocus();
								ImGui::PopID();
							}
							ImGui::EndCombo();
						}
						ImGui::PopItemWidth();

						FVector2 Size = BillboardComp->GetSize();
						float SizeArr[2] = { Size.X, Size.Y };
						if (ImGui::DragFloat2("Size", SizeArr, 0.01f, 0.01f, 100.f, "%.2f"))
							BillboardComp->SetSize(FVector2(SizeArr[0], SizeArr[1]));

						ImGui::Unindent(8.0f);
					}
				}

				// ─── USubUVComponent ───
				else if (SelectedComponent->IsA(USubUVComponent::StaticClass()))
				{
					USubUVComponent* SubUVComp = static_cast<USubUVComponent*>(SelectedComponent);

					if (ImGui::CollapsingHeader("SubUV", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Indent(8.0f);
						bool bBillboard = SubUVComp->IsBillboard();
						if (ImGui::Checkbox("Billboard", &bBillboard))
							SubUVComp->SetBillboard(bBillboard);
						ImGui::Unindent(8.0f);
					}
				}

				// ─── UTextRenderComponent ───
				else if (SelectedComponent->IsA(UTextRenderComponent::StaticClass())
					&& !SelectedComponent->IsA(UUUIDBillboardComponent::StaticClass()))
				{
					UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(SelectedComponent);

					if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Indent(8.0f);
						bool bBillboard = TextComp->IsBillboard();
						if (ImGui::Checkbox("Billboard", &bBillboard))
							TextComp->SetBillboard(bBillboard);
						ImGui::Unindent(8.0f);
					}
				}

				// ─── UStaticMeshComponent ───
				else if (SelectedComponent->IsA(UStaticMeshComponent::StaticClass()))
				{
					UStaticMeshComponent* MeshComp = static_cast<UStaticMeshComponent*>(SelectedComponent);

					if (ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Indent(8.0f);
						UStaticMesh* CurrentMesh = MeshComp->GetStaticMesh();
						std::string CurrentMeshName = CurrentMesh ? CurrentMesh->GetAssetPathFileName() : "None";

						ImGui::Text("Mesh Asset:");
						ImGui::SameLine();
						ImGui::PushItemWidth(200.f);
						if (ImGui::BeginCombo("##StaticMeshAssign", CurrentMeshName.c_str()))
						{
							for (TObjectIterator<UStaticMesh> It; It; ++It)
							{
								UStaticMesh* MeshAsset = It.Get();
								if (!MeshAsset) continue;
								std::string MeshName = MeshAsset->GetAssetPathFileName();
								bool bSelected = (CurrentMesh == MeshAsset);
								if (ImGui::Selectable(MeshName.c_str(), bSelected))
									MeshComp->SetStaticMesh(MeshAsset);
								if (bSelected) ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
						ImGui::PopItemWidth();
						ImGui::Unindent(8.0f);
					}

					if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::Indent(8.0f);
						if (UStaticMesh* MeshData = MeshComp->GetStaticMesh())
						{
							TArray<FString> MatNames = FMaterialManager::Get().GetAllMaterialNames();
							uint32 NumSections = MeshData->GetNumSections();

							// 전체 일괄 변경
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
							ImGui::Text("Apply to All Sections:");
							ImGui::PopStyleColor();
							ImGui::SameLine();
							ImGui::PushItemWidth(180.f);
							if (ImGui::BeginCombo("##SetAllMaterials", "Select Material..."))
							{
								for (const FString& MatName : MatNames)
								{
									ImGui::PushID(MatName.c_str());
									auto ListMaterial = FMaterialManager::Get().FindByName(MatName);
									ImTextureID TexID = (ImTextureID)0;
									if (ListMaterial && ListMaterial->GetMaterialTexture()
										&& ListMaterial->GetMaterialTexture()->TextureSRV)
										TexID = (ImTextureID)ListMaterial->GetMaterialTexture()->TextureSRV;
									if (TexID) { ImGui::Image(TexID, ImVec2(24.f, 24.f)); ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f); }
									if (ImGui::Selectable(MatName.c_str(), false))
										if (ListMaterial)
											for (uint32 j = 0; j < NumSections; ++j)
												MeshComp->SetMaterial(j, ListMaterial);
									ImGui::PopID();
								}
								ImGui::EndCombo();
							}
							ImGui::PopItemWidth();

							// UV 스크롤 전체
							float MasterScroll[4] = {};
							if (NumSections > 0)
								if (auto FirstMat = MeshComp->GetMaterial(0))
									FirstMat->GetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
							ImGui::PushItemWidth(180.f);
							if (ImGui::DragFloat2("Scroll All", MasterScroll, 0.001f, -5.f, 5.f, "%.2f"))
								for (uint32 j = 0; j < NumSections; ++j)
									if (auto Mat = MeshComp->GetMaterial(j))
										Mat->SetParameterData("UVScrollSpeed", MasterScroll, sizeof(MasterScroll));
							ImGui::PopItemWidth();

							ImGui::Separator();
							ImGui::Spacing();

							// 섹션별
							for (uint32 i = 0; i < NumSections; ++i)
							{
								auto CurrentMat = MeshComp->GetMaterial(i);
								std::string CurrentMatName = CurrentMat ? CurrentMat->GetOriginName() : "None";

								ImGui::PushID(i);
								std::string Label = "Section " + std::to_string(i);
								ImGui::PushItemWidth(180.f);
								if (ImGui::BeginCombo(Label.c_str(), CurrentMatName.c_str()))
								{
									for (const FString& MatName : MatNames)
									{
										ImGui::PushID(MatName.c_str());
										auto ListMaterial = FMaterialManager::Get().FindByName(MatName);
										ImTextureID TexID = (ImTextureID)0;
										if (ListMaterial && ListMaterial->GetMaterialTexture()
											&& ListMaterial->GetMaterialTexture()->TextureSRV)
											TexID = (ImTextureID)ListMaterial->GetMaterialTexture()->TextureSRV;
										if (TexID) { ImGui::Image(TexID, ImVec2(24.f, 24.f)); ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f); }
										bool bSelected = (CurrentMatName == MatName);
										if (ImGui::Selectable(MatName.c_str(), bSelected))
											if (ListMaterial) MeshComp->SetMaterial(i, ListMaterial);
										if (bSelected) ImGui::SetItemDefaultFocus();
										ImGui::PopID();
									}
									ImGui::EndCombo();
								}
								ImGui::PopItemWidth();

								if (CurrentMat)
								{
									FVector4 MatColor = CurrentMat->GetVectorParameter("BaseColor");
									float ColorArray[4] = { MatColor.X, MatColor.Y, MatColor.Z, MatColor.W };
									ImGui::PushID(i + 1000);
									if (ImGui::ColorEdit4("Base Color", ColorArray))
										CurrentMat->SetParameterData("BaseColor", ColorArray, sizeof(ColorArray));
									ImGui::PopID();

									if (auto MatTex = CurrentMat->GetMaterialTexture())
									{
										float SpeedArray[4] = {};
										CurrentMat->GetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));
										ImGui::PushID(i + 2000);
										if (ImGui::DragFloat2("UV Scroll", SpeedArray, 0.001f, -5.f, 5.f, "%.2f"))
											CurrentMat->SetParameterData("UVScrollSpeed", SpeedArray, sizeof(SpeedArray));
										ImGui::PopID();
									}
								}
								ImGui::PopID();
								ImGui::Spacing();
							}
						}
						else
						{
							ImGui::TextDisabled("No Static Mesh Assigned");
						}
						ImGui::Unindent(8.0f);
					}
				}
			}
		}
	}
	ImGui::End();
}
