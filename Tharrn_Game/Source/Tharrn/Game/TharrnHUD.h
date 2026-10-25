#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TharrnHUD.generated.h"

class ATharrnPlayerCharacter;
class UTharrnInventoryComponent;

/**
 * Greybox HUD drawn on the canvas: vitals, load, hotbar, interaction prompt and progress, pack and container panels
 * (click to use or move), creature health and telegraph markers, combat feedback, notifications and a controls sheet.
 * Placeholder presentation; the final UI will be UMG over the same data.
 */
UCLASS()
class THARRN_API ATharrnHUD : public AHUD
{
	GENERATED_BODY()

public:
	static ATharrnHUD* Get(const AActor* Context);

	void PushMessage(const FString& Message);
	/** Mouse click while a panel is open. */
	void HandleClick();

	bool bShowHelp = false;
	/** Optional caption (used by captured test screenshots). */
	FString Caption;

	virtual void DrawHUD() override;

protected:
	struct FMessage
	{
		FString Text;
		float Time = 0.f;
	};
	struct FRegion
	{
		FBox2D Box;
		TFunction<void()> Action;
	};

	float S() const;
	void Text(const FString& Value, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bLarge = false);
	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color, const FString& Label);
	void Button(const FString& Label, float X, float Y, float W, float H, const FLinearColor& Color, TFunction<void()> Action);

	void DrawVitals(ATharrnPlayerCharacter* Player);
	void DrawHotbar(ATharrnPlayerCharacter* Player);
	void DrawPrompt(ATharrnPlayerCharacter* Player);
	void DrawPackPanel(ATharrnPlayerCharacter* Player, float X, float Y);
	void DrawContainerPanel(ATharrnPlayerCharacter* Player, float X, float Y);
	void DrawWorldMarkers(ATharrnPlayerCharacter* Player);
	void DrawMessages();
	void DrawHelp();
	FString SlotText(const UTharrnInventoryComponent* Inventory, int32 Index) const;

	TArray<FMessage> Messages;
	TArray<FRegion> Regions;
};
